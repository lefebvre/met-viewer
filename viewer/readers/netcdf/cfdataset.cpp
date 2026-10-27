#include "viewer/readers/netcdf/cfdataset.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <netcdf.h>

#include "viewer/core/log.h"
#include "viewer/core/pathutil.h"
#include "viewer/core/timeaxis.h"

namespace met::readers::netcdf {
namespace {

using core::RegularLatLonGrid;
using core::TimePoint;
using core::VerticalLevel;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<std::string> attText(int ncid, int varid, const char* name) {
    std::size_t len = 0;
    nc_type type = NC_NAT;
    if (nc_inq_att(ncid, varid, name, &type, &len) != NC_NOERR) return std::nullopt;
    // NetCDF-4 string attributes (what h5netcdf and h5py write) are NC_STRING,
    // which nc_get_att_text refuses; reading only NC_CHAR dropped `units` and
    // `calendar` from such files without a word.
    if (type == NC_STRING) {
        if (len == 0) return std::string();
        std::vector<char*> parts(len, nullptr);
        if (nc_get_att_string(ncid, varid, name, parts.data()) != NC_NOERR) return std::nullopt;
        std::string s = parts[0] ? parts[0] : "";
        nc_free_string(len, parts.data());
        return s;
    }
    if (type != NC_CHAR) return std::nullopt;
    std::string s(len, '\0');
    if (nc_get_att_text(ncid, varid, name, s.data()) != NC_NOERR) return std::nullopt;
    // Trim a trailing NUL if present.
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::optional<double> attDouble(int ncid, int varid, const char* name) {
    int id = 0;
    if (nc_inq_attid(ncid, varid, name, &id) != NC_NOERR) return std::nullopt;
    double v = 0;
    if (nc_get_att_double(ncid, varid, name, &v) != NC_NOERR) return std::nullopt;
    return v;
}

// Every value of a numeric attribute (missing_value and valid_range may hold
// several).
std::vector<double> attDoubles(int ncid, int varid, const char* name) {
    std::size_t len = 0;
    if (nc_inq_attlen(ncid, varid, name, &len) != NC_NOERR || len == 0) return {};
    std::vector<double> v(len);
    if (nc_get_att_double(ncid, varid, name, v.data()) != NC_NOERR) return {};
    return v;
}

// netCDF's default fill value for a type: what a cell holds when nothing was
// ever written to it. CF treats it as missing when no _FillValue is given. Bytes
// are excluded, as netCDF's own guidance says, since their whole range is
// commonly used for data.
std::optional<double> defaultFill(nc_type type) {
    switch (type) {
        case NC_SHORT: return NC_FILL_SHORT;
        case NC_USHORT: return NC_FILL_USHORT;
        case NC_INT: return NC_FILL_INT;
        case NC_UINT: return NC_FILL_UINT;
        case NC_INT64: return static_cast<double>(NC_FILL_INT64);
        case NC_UINT64: return static_cast<double>(NC_FILL_UINT64);
        case NC_FLOAT: return static_cast<double>(NC_FILL_FLOAT);
        case NC_DOUBLE: return NC_FILL_DOUBLE;
        default: return std::nullopt;
    }
}

int integerBits(nc_type type) {
    switch (type) {
        case NC_BYTE: return 8;
        case NC_SHORT: return 16;
        case NC_INT: return 32;
        default: return 0;
    }
}

// A signed stored value read back as the unsigned one it encodes.
double asUnsigned(double raw, int bits) { return raw < 0 ? raw + std::ldexp(1.0, bits) : raw; }

bool isLatUnits(const std::string& u) {
    const std::string l = lower(u);
    return l == "degrees_north" || l == "degree_north" || l == "degrees_n" || l == "degreesn";
}
bool isLonUnits(const std::string& u) {
    const std::string l = lower(u);
    return l == "degrees_east" || l == "degree_east" || l == "degrees_e" || l == "degreese";
}

// An ensemble-member axis: CF's standard_name, or one of the names producers use
// for it (ERA5 ensembles call it `number`).
bool isMemberAxis(const std::string& name, const std::string& standardName) {
    const std::string n = lower(name);
    return lower(standardName) == "realization" || n == "number" || n == "realization" ||
           n == "member" || n == "ensemble_member" || n == "ensemble" || n == "ens";
}

// RecordHandle layout: member index in the top 16 bits, then level and time
// index in 24 bits each.
constexpr int kLevelShift = 24;
constexpr int kMemberShift = 48;
constexpr core::RecordHandle kIndexMask = 0xFFFFFF;

// Classify a vertical coordinate from its metadata. Returns false if the
// coordinate does not look vertical. On success `type` is the level type and
// `scale` converts the stored value into that type's unit (hPa for pressure,
// metres for height; 1.0 otherwise). Recognizing height/sigma/hybrid/model/
// isentropic axes — not just pressure — is what keeps a multi-level variable
// from collapsing to a single slice.
//
// The evidence is weighed strongest first: standard_name, then units, then the
// variable name. The order matters because names are reused across conventions.
// CMIP calls its hybrid sigma-pressure axis `lev` (values near 0.99, unit "1"),
// and a name-first match read every one of those levels as "0.99 hPa".
bool classifyVertical(const std::string& units, const std::string& standardName,
                      const std::string& name, bool hasPositive, VerticalLevel::Type& type,
                      double& scale) {
    const std::string u = lower(units);
    const std::string s = lower(standardName);
    const std::string n = lower(name);
    scale = 1.0;

    const bool pressureUnit =
        u == "hpa" || u == "pa" || u == "millibar" || u == "mb" || u == "mbar";
    const bool lengthUnit = u == "m" || u == "metre" || u == "metres" || u == "meter" ||
                            u == "meters" || u == "km";
    auto set = [&](VerticalLevel::Type t) {
        type = t;
        if (t == VerticalLevel::Type::PressureHPa) scale = (u == "pa") ? 0.01 : 1.0;
        if (t == VerticalLevel::Type::HeightM) scale = (u == "km") ? 1000.0 : 1.0;
        return true;
    };

    // 1. standard_name says what the axis is.
    if (s.find("air_pressure") != std::string::npos) return set(VerticalLevel::Type::PressureHPa);
    if (s == "atmosphere_hybrid_sigma_pressure_coordinate") return set(VerticalLevel::Type::Hybrid);
    if (s == "atmosphere_sigma_coordinate") return set(VerticalLevel::Type::Sigma);
    if (s == "atmosphere_hybrid_height_coordinate" || s == "height" || s == "altitude" ||
        s == "geopotential_height")
        return set(VerticalLevel::Type::HeightM);
    if (s == "air_potential_temperature") return set(VerticalLevel::Type::Isentropic);
    if (s == "model_level_number") return set(VerticalLevel::Type::ModelLevel);

    // 2. Units that fix the quantity.
    if (pressureUnit) return set(VerticalLevel::Type::PressureHPa);
    if (lengthUnit) return set(VerticalLevel::Type::HeightM);

    // 3. The name alone.
    if (n == "hybrid" || n == "hyblevel") return set(VerticalLevel::Type::Hybrid);
    if (n == "sigma") return set(VerticalLevel::Type::Sigma);
    if (n == "pressure_level" || n == "plev" || n == "level" || n == "lev" ||
        n == "isobaricinhpa" || n == "isobaric")
        return set(VerticalLevel::Type::PressureHPa);
    if (n == "height" || n == "altitude") return set(VerticalLevel::Type::HeightM);
    if (n == "theta" || n == "isentropic" || n == "pt") return set(VerticalLevel::Type::Isentropic);
    if (n == "model_level" || n == "model_level_number" || n == "nlev")
        return set(VerticalLevel::Type::ModelLevel);
    // A CF `positive: up|down` attribute marks a generic vertical axis; accept it
    // as an unknown-typed level rather than silently dropping the dimension.
    if (hasPositive) {
        type = VerticalLevel::Type::Unknown;
        return true;
    }
    return false;
}

// A CF time axis: "<unit> since <reference>" plus the calendar it counts in.
struct TimeUnits {
    enum class Calendar { Gregorian, NoLeap, AllLeap, Day360 };
    double perUnit = 1.0;  // seconds per unit
    int y = 1970, mo = 1, d = 1, hh = 0, mm = 0, ss = 0;
    Calendar calendar = Calendar::Gregorian;
};

// Up to six non-negative integers from "YYYY-MM-DD hh:mm:ss", separated by any
// of "-: T". A leading '-' on the year is honoured. Fractional seconds and a
// trailing zone ("Z", "+00:00") end the scan; CF reference times are UTC.
int parseReference(const std::string& ref, int (&out)[6]) {
    std::size_t i = 0;
    while (i < ref.size() && ref[i] == ' ') ++i;
    int n = 0;
    while (n < 6 && i < ref.size()) {
        const bool negative = n == 0 && ref[i] == '-';
        if (negative) ++i;
        int value = 0;
        const auto [end, ec] = std::from_chars(ref.data() + i, ref.data() + ref.size(), value);
        if (ec != std::errc{}) break;
        out[n++] = negative ? -value : value;
        i = static_cast<std::size_t>(end - ref.data());
        // Separators between fields; a date field must be followed by '-' and a
        // time field by ':', except at the date/time boundary.
        if (i >= ref.size()) break;
        const char sep = ref[i];
        const bool ok = (n < 3 && sep == '-') || (n == 3 && (sep == ' ' || sep == 'T' || sep == 't')) ||
                        (n > 3 && n < 6 && sep == ':');
        if (!ok) break;
        ++i;
        while (n == 3 && i < ref.size() && ref[i] == ' ') ++i;
    }
    return n;
}

// Parse a CF time-unit string "<unit> since <YYYY-MM-DD[ hh:mm:ss]>" and the
// calendar attribute. An unrecognized unit or reference is reported rather than
// silently read as seconds since 1970.
TimeUnits parseTimeUnits(const std::string& units, const std::string& calendar,
                         const std::filesystem::path& path) {
    TimeUnits tu;
    const std::string l = lower(units);
    const auto pos = l.find(" since ");
    if (pos == std::string::npos) {
        MET_LOG_WARN("{}: time units '{}' are not '<unit> since <date>'; reading them as "
                     "seconds since 1970-01-01",
                     core::pathToUtf8(path), units);
        return tu;
    }

    std::string unitWord = l.substr(0, pos);
    while (!unitWord.empty() && unitWord.front() == ' ') unitWord.erase(0, 1);
    auto is = [&](std::initializer_list<const char*> words) {
        for (const char* w : words)
            if (unitWord == w) return true;
        return false;
    };
    // CF's month and year are UDUNITS' fixed intervals (a year of 365.242198781
    // days and a twelfth of it), not calendar months; CF itself discourages them.
    constexpr double kUdunitsYear = 365.242198781 * 86400.0;
    if (is({"s", "sec", "secs", "second", "seconds"})) tu.perUnit = 1.0;
    else if (is({"min", "mins", "minute", "minutes"})) tu.perUnit = 60.0;
    else if (is({"h", "hr", "hrs", "hour", "hours"})) tu.perUnit = 3600.0;
    else if (is({"d", "day", "days"})) tu.perUnit = 86400.0;
    else if (is({"week", "weeks"})) tu.perUnit = 7 * 86400.0;
    else if (is({"month", "months"}) || is({"year", "years", "yr", "yrs"})) {
        tu.perUnit = unitWord[0] == 'm' ? kUdunitsYear / 12.0 : kUdunitsYear;
        MET_LOG_WARN("{}: time is counted in '{}', which CF defines as a fixed UDUNITS "
                     "interval, not a calendar {}; times may not fall on the dates the "
                     "producer intended",
                     core::pathToUtf8(path), unitWord, unitWord[0] == 'm' ? "month" : "year");
    } else {
        MET_LOG_WARN("{}: unrecognized time unit '{}'; reading it as seconds", core::pathToUtf8(path),
                     unitWord);
    }

    int f[6] = {1970, 1, 1, 0, 0, 0};
    // A bare date (no time-of-day) is valid; require at least Y-M-D to trust it.
    if (parseReference(units.substr(pos + 7), f) < 3) {
        MET_LOG_WARN("{}: cannot read the reference date in time units '{}'; using "
                     "1970-01-01",
                     core::pathToUtf8(path), units);
        f[0] = 1970, f[1] = 1, f[2] = 1, f[3] = f[4] = f[5] = 0;
    }
    tu.y = f[0], tu.mo = f[1], tu.d = f[2], tu.hh = f[3], tu.mm = f[4], tu.ss = f[5];

    const std::string cal = lower(calendar);
    if (cal.empty() || cal == "standard" || cal == "gregorian" || cal == "proleptic_gregorian") {
        tu.calendar = TimeUnits::Calendar::Gregorian;
    } else if (cal == "noleap" || cal == "365_day") {
        tu.calendar = TimeUnits::Calendar::NoLeap;
    } else if (cal == "all_leap" || cal == "366_day") {
        tu.calendar = TimeUnits::Calendar::AllLeap;
    } else if (cal == "360_day") {
        tu.calendar = TimeUnits::Calendar::Day360;
    } else {
        MET_LOG_WARN("{}: calendar '{}' is not supported; times are converted as if it "
                     "were Gregorian and may be off by days",
                     core::pathToUtf8(path), calendar);
    }
    return tu;
}

// Cumulative days before each month in a 365- or 366-day year.
constexpr int kCumNoLeap[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
constexpr int kCumAllLeap[13] = {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366};

// The valid time `offset` units after the reference, or nullopt when the offset
// is not a usable number (a fill value, NaN, or beyond any plausible date).
//
// Model calendars are counted in their own day arithmetic and the resulting
// calendar date is then placed on the real time line. A 360-day "February 30"
// therefore lands on the first days of March: TimePoint has no other way to
// name it, and the alternative -- counting model days as real ones -- drifts by
// five or six days a year.
std::optional<TimePoint> validTime(const TimeUnits& tu, double offset) {
    const double secs = offset * tu.perUnit;
    constexpr double kLimit = 1e13;  // ~300,000 years
    if (!std::isfinite(secs) || std::abs(secs) > kLimit) return std::nullopt;

    if (tu.calendar == TimeUnits::Calendar::Gregorian)
        return TimePoint{core::timegmUtc(tu.y, tu.mo, tu.d, tu.hh, tu.mm, tu.ss) +
                         static_cast<std::int64_t>(std::llround(secs))};

    const double total = tu.hh * 3600.0 + tu.mm * 60.0 + tu.ss + secs;
    const auto days = static_cast<std::int64_t>(std::floor(total / 86400.0));
    const double secOfDay = total - static_cast<double>(days) * 86400.0;

    const bool d360 = tu.calendar == TimeUnits::Calendar::Day360;
    const int* cum = tu.calendar == TimeUnits::Calendar::AllLeap ? kCumAllLeap : kCumNoLeap;
    const int yearLen = d360 ? 360 : cum[12];
    const int mo = std::clamp(tu.mo, 1, 12);
    const std::int64_t start = static_cast<std::int64_t>(tu.y) * yearLen +
                               (d360 ? (mo - 1) * 30 : cum[mo - 1]) + (tu.d - 1);
    const std::int64_t n = start + days;
    const std::int64_t y = (n >= 0 ? n : n - (yearLen - 1)) / yearLen;  // floor division
    const int doy = static_cast<int>(n - y * yearLen);
    int m = 1, d = 1;
    if (d360) {
        m = doy / 30 + 1;
        d = doy % 30 + 1;
    } else {
        while (m < 12 && cum[m] <= doy) ++m;
        d = doy - cum[m - 1] + 1;
    }
    return TimePoint{core::timegmUtc(static_cast<int>(y), m, d, 0, 0, 0) +
                     static_cast<std::int64_t>(std::llround(secOfDay))};
}

// Reject a coordinate axis that is not evenly spaced, since RegularLatLonGrid can
// only describe a uniform one. A small tolerance (1% of the step) absorbs the
// rounding in a float32-stored axis without admitting a genuinely Gaussian or
// stretched one, whose steps vary by whole percent toward the poles.
//
// Each step is compared with the first, and each value with where the mean step
// puts it. The second check catches a slow drift that stays under the tolerance
// one step at a time but adds up across the axis.
void checkUniform(const std::vector<double>& axis, const char* what,
                  const std::filesystem::path& path) {
    for (double v : axis)
        if (!std::isfinite(v))
            throw ReadError(std::string("NetCDF: ") + what + " coordinate holds a value that is "
                            "not a number: " + core::pathToUtf8(path));
    if (axis.size() < 3) return;  // two points just define the spacing
    const double step = axis[1] - axis[0];
    if (step == 0.0)
        throw ReadError(std::string("NetCDF: ") + what + " axis has a zero step in " +
                        core::pathToUtf8(path));
    const double tol = std::abs(step) * 0.01;
    const double mean = (axis.back() - axis.front()) / static_cast<double>(axis.size() - 1);
    for (std::size_t i = 1; i < axis.size(); ++i) {
        const double d = axis[i] - axis[i - 1];
        const double drift = axis[i] - (axis.front() + mean * static_cast<double>(i));
        if (std::abs(d - step) > tol || std::abs(drift) > tol) {
            MET_LOG_ERROR("{}: {} axis is not evenly spaced (step {} at index {}, expected {})",
                          core::pathToUtf8(path), what, d, i, step);
            throw ReadError(std::string("NetCDF: ") + what +
                            " axis is not evenly spaced; irregular and Gaussian grids are "
                            "not supported: " +
                            core::pathToUtf8(path));
        }
    }
}

}  // namespace

std::mutex& CfDataset::libraryMutex() {
    static std::mutex m;
    return m;
}

CfDataset::CfDataset(std::filesystem::path path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lock(libraryMutex());
    // Not UTF-8: on Windows netcdf-c reads the path in the system code page.
    if (nc_open(core::pathForCodePageApi(path_).c_str(), NC_NOWRITE, &ncid_) != NC_NOERR)
        throw ReadError("NetCDF: cannot open " + core::pathToUtf8(path_));
    try {
        scan();
    } catch (...) {
        nc_close(ncid_);
        ncid_ = -1;
        throw;
    }
}

CfDataset::~CfDataset() {
    std::lock_guard<std::mutex> lock(libraryMutex());
    if (ncid_ >= 0) nc_close(ncid_);
}

void CfDataset::scan() {
    int ndims = 0, nvars = 0, ngatts = 0, unlim = 0;
    if (nc_inq(ncid_, &ndims, &nvars, &ngatts, &unlim) != NC_NOERR)
        throw ReadError("NetCDF: nc_inq failed");

    // Dimension names.
    std::vector<std::string> dimName(static_cast<std::size_t>(ndims));
    std::vector<std::size_t> dimLen(static_cast<std::size_t>(ndims));
    for (int d = 0; d < ndims; ++d) {
        char nm[NC_MAX_NAME + 1] = {0};
        std::size_t len = 0;
        nc_inq_dim(ncid_, d, nm, &len);
        dimName[static_cast<std::size_t>(d)] = nm;
        dimLen[static_cast<std::size_t>(d)] = len;
    }

    // Identify coordinate-variable roles per dimension.
    int latDim = -1, lonDim = -1, timeDim = -1, levelDim = -1, memberDim = -1;
    int latVar = -1, lonVar = -1, timeVar = -1, levelVar = -1, memberVar = -1;

    auto classifyCoord = [&](int varid, const std::string& name, int dimid) {
        const std::string units = attText(ncid_, varid, "units").value_or("");
        const std::string std = lower(attText(ncid_, varid, "standard_name").value_or(""));
        const std::string ln = lower(name);
        if (latDim < 0 &&
            (isLatUnits(units) || std == "latitude" || ln == "latitude" || ln == "lat")) {
            latDim = dimid;
            latVar = varid;
            return;
        }
        if (lonDim < 0 &&
            (isLonUnits(units) || std == "longitude" || ln == "longitude" || ln == "lon")) {
            lonDim = dimid;
            lonVar = varid;
            return;
        }
        if (timeDim < 0 &&
            (std == "time" || ln == "time" || lower(units).find(" since ") != std::string::npos)) {
            timeDim = dimid;
            timeVar = varid;
            return;
        }
        if (memberDim < 0 && isMemberAxis(name, std)) {
            memberDim = dimid;
            memberVar = varid;
            return;
        }
        if (levelDim < 0) {
            const bool hasPositive = attText(ncid_, varid, "positive").has_value();
            VerticalLevel::Type t = VerticalLevel::Type::Unknown;
            double sc = 1.0;
            if (classifyVertical(units, std, name, hasPositive, t, sc)) {
                levelDim = dimid;
                levelVar = varid;
            }
        }
    };

    for (int v = 0; v < nvars; ++v) {
        char nm[NC_MAX_NAME + 1] = {0};
        nc_type type;
        int vndims = 0, dimids[NC_MAX_VAR_DIMS] = {0}, vnatts = 0;
        nc_inq_var(ncid_, v, nm, &type, &vndims, dimids, &vnatts);
        if (vndims == 1 && dimName[static_cast<std::size_t>(dimids[0])] == nm) {
            classifyCoord(v, nm, dimids[0]);
        }
    }
    // An ensemble dimension need not carry a coordinate variable.
    for (int d = 0; d < ndims && memberDim < 0; ++d)
        if (d != latDim && d != lonDim && d != timeDim && d != levelDim &&
            isMemberAxis(dimName[static_cast<std::size_t>(d)], ""))
            memberDim = d;

    if (latDim < 0 || lonDim < 0)
        throw ReadError("NetCDF: no latitude/longitude coordinates found");

    // Read coordinate arrays.
    std::vector<double> lat(dimLen[static_cast<std::size_t>(latDim)]);
    std::vector<double> lon(dimLen[static_cast<std::size_t>(lonDim)]);
    nc_get_var_double(ncid_, latVar, lat.data());
    nc_get_var_double(ncid_, lonVar, lon.data());

    std::vector<double> levels;
    VerticalLevel::Type levelType = VerticalLevel::Type::Surface;
    double levelScale = 1.0;
    if (levelDim >= 0) {
        levels.resize(dimLen[static_cast<std::size_t>(levelDim)]);
        nc_get_var_double(ncid_, levelVar, levels.data());
        char lvName[NC_MAX_NAME + 1] = {0};
        nc_inq_varname(ncid_, levelVar, lvName);
        const std::string lu = attText(ncid_, levelVar, "units").value_or("");
        const std::string lstd = attText(ncid_, levelVar, "standard_name").value_or("");
        const bool hasPositive = attText(ncid_, levelVar, "positive").has_value();
        if (!classifyVertical(lu, lstd, lvName, hasPositive, levelType, levelScale)) {
            // Detected as the level axis but unclassified — assume pressure.
            levelType = VerticalLevel::Type::PressureHPa;
            levelScale = (lower(lu) == "pa") ? 0.01 : 1.0;
        }
    }

    // A level whose coordinate is NaN cannot be ordered or matched (NaN breaks
    // the strict weak ordering the catalog sorts by, and never compares equal),
    // so its slabs are left out.
    std::vector<bool> levelUsable(levels.size(), true);
    for (std::size_t i = 0; i < levels.size(); ++i)
        if (!std::isfinite(levels[i])) {
            levelUsable[i] = false;
            MET_LOG_WARN("{}: vertical coordinate {} is not a number; skipping that level",
                         core::pathToUtf8(path_), i);
        }

    // Valid times. One whose value is a fill or otherwise unusable is skipped
    // rather than converted: llround of 9.97e36 is undefined, and the sum with the
    // reference overflowed.
    std::vector<std::optional<TimePoint>> times;
    if (timeDim >= 0) {
        std::vector<double> raw(dimLen[static_cast<std::size_t>(timeDim)]);
        nc_get_var_double(ncid_, timeVar, raw.data());
        const TimeUnits tu = parseTimeUnits(
            attText(ncid_, timeVar, "units").value_or("seconds since 1970-01-01"),
            attText(ncid_, timeVar, "calendar").value_or(""), path_);
        times.reserve(raw.size());
        for (std::size_t i = 0; i < raw.size(); ++i) {
            times.push_back(validTime(tu, raw[i]));
            if (!times.back())
                MET_LOG_WARN("{}: time {} ({}) is not a usable value; skipping that time",
                             core::pathToUtf8(path_), i, raw[i]);
        }
    }

    // Ensemble member numbers: the coordinate's values when it has one, else the
    // position along the dimension.
    std::vector<int> members;
    if (memberDim >= 0) {
        members.resize(dimLen[static_cast<std::size_t>(memberDim)]);
        if (memberVar < 0 || nc_get_var_int(ncid_, memberVar, members.data()) != NC_NOERR)
            for (std::size_t m = 0; m < members.size(); ++m) members[m] = static_cast<int>(m);
    }

    if (lat.empty() || lon.empty()) throw ReadError("NetCDF: empty latitude/longitude coordinate");

    // Build the grid geometry from the coordinate arrays. RegularLatLonGrid is a
    // uniform-spacing model, so verify the axes actually are uniform instead of
    // extrapolating from the first two values: a Gaussian latitude axis (common
    // when ERA5 GRIB is converted to NetCDF) would otherwise be silently
    // mis-georeferenced by up to a grid cell toward the poles, with no symptom
    // beyond values appearing at the wrong place.
    checkUniform(lat, "latitude", path_);
    checkUniform(lon, "longitude", path_);

    // The spacing is the mean step over the whole axis rather than the first
    // one, which a float32-stored axis can carry rounding in.
    auto meanStep = [](const std::vector<double>& axis, double fallback) {
        return axis.size() > 1 ? (axis.back() - axis.front()) / static_cast<double>(axis.size() - 1)
                               : fallback;
    };
    RegularLatLonGrid g;
    g.nlat = static_cast<int>(lat.size());
    g.nlon = static_cast<int>(lon.size());
    g.lat0 = lat.front();
    g.lon0 = lon.front();
    g.dlat = meanStep(lat, -1.0);
    g.dlon = meanStep(lon, 1.0);
    g.globalWrapLon = std::abs(g.dlon) * g.nlon >= 359.0;
    grid_ = g;

    auto levelAt = [&](std::size_t i) -> VerticalLevel {
        if (levels.empty()) return VerticalLevel{VerticalLevel::Type::Surface, 0.0};
        return VerticalLevel{levelType, levels[i] * levelScale};
    };

    // Data variables: any non-coordinate var whose dims include lat and lon.
    for (int v = 0; v < nvars; ++v) {
        char nm[NC_MAX_NAME + 1] = {0};
        nc_type type;
        int vndims = 0, dimids[NC_MAX_VAR_DIMS] = {0}, vnatts = 0;
        nc_inq_var(ncid_, v, nm, &type, &vndims, dimids, &vnatts);
        if (v == latVar || v == lonVar || v == timeVar || v == levelVar || v == memberVar) continue;

        int latAxis = -1, lonAxis = -1, timeAxis = -1, levelAxis = -1, memberAxis = -1;
        std::string unselectable;  // a dimension of length > 1 nothing here can index
        for (int a = 0; a < vndims; ++a) {
            const int d = dimids[a];
            if (d == latDim) latAxis = a;
            else if (d == lonDim) lonAxis = a;
            else if (d == timeDim) timeAxis = a;
            else if (d == levelDim) levelAxis = a;
            else if (d == memberDim) memberAxis = a;
            else if (dimLen[static_cast<std::size_t>(d)] > 1 && unselectable.empty())
                unselectable = dimName[static_cast<std::size_t>(d)];
        }
        if (latAxis < 0 || lonAxis < 0) continue;  // not a griddable field
        // A second vertical axis (`isobaric1`), a spectral band, anything else with
        // more than one entry: reading index 0 of it and presenting the slab as the
        // whole variable is a silently wrong field, so the variable is left out.
        if (!unselectable.empty()) {
            MET_LOG_WARN("{}: skipping '{}': its dimension '{}' is not a time, vertical or "
                         "ensemble axis, so no single slab of it can be shown",
                         core::pathToUtf8(path_), nm, unselectable);
            continue;
        }

        VarInfo info;
        info.varid = v;
        info.ndims = vndims;
        info.latAxis = latAxis;
        info.lonAxis = lonAxis;
        info.timeAxis = timeAxis;
        info.levelAxis = levelAxis;
        info.memberAxis = memberAxis;
        info.units = attText(ncid_, v, "units").value_or("");
        info.longName = attText(ncid_, v, "long_name").value_or("");
        info.standardName = attText(ncid_, v, "standard_name").value_or("");
        if (auto s = attDouble(ncid_, v, "scale_factor")) {
            info.scale = *s;
            info.hasScale = true;
        }
        if (auto o = attDouble(ncid_, v, "add_offset")) {
            info.offset = *o;
            info.hasOffset = true;
        }
        // Missing data. Without a _FillValue attribute the type's default fill
        // still marks never-written cells, unless the variable was defined in
        // no-fill mode; ignoring it let 9.97e36 through as data and blew out the
        // colour range. missing_value is honoured alongside, and may list several.
        if (auto f = attDouble(ncid_, v, "_FillValue")) {
            info.missing.push_back(*f);
        } else {
            int noFill = 0;
            if (nc_inq_var_fill(ncid_, v, &noFill, nullptr) == NC_NOERR && !noFill)
                if (auto d = defaultFill(type)) info.missing.push_back(*d);
        }
        for (double m : attDoubles(ncid_, v, "missing_value")) info.missing.push_back(m);

        const auto unsignedAttr = attText(ncid_, v, "_Unsigned");
        if (unsignedAttr && lower(*unsignedAttr) == "true") info.unsignedBits = integerBits(type);

        const std::vector<double> range = attDoubles(ncid_, v, "valid_range");
        if (range.size() == 2) {
            info.validMin = range[0];
            info.validMax = range[1];
        } else {
            if (auto lo = attDouble(ncid_, v, "valid_min")) info.validMin = *lo;
            if (auto hi = attDouble(ncid_, v, "valid_max")) info.validMax = *hi;
        }
        if (info.unsignedBits > 0) {
            info.validMin = asUnsigned(info.validMin, info.unsignedBits);
            info.validMax = asUnsigned(info.validMax, info.unsignedBits);
        }
        vars_[nm] = info;

        // A variable is listed only along the axes it actually has. Without a
        // level dimension it is a single-level field (PS beside T(lev) in a
        // MERRA-2 file): listing it at every file level, as this once did, put
        // one identical slab at each "hPa" and drew a fake constant profile.
        //
        // Time is the exception. A variable without a time dimension is
        // time-invariant (orography, a land mask), so it is equally true at every
        // time the file covers, and listing it there keeps it on the same time
        // axis as everything else instead of at 1970-01-01.
        const bool hasLevel = levelAxis >= 0 && !levels.empty();
        const std::size_t nLev = hasLevel ? levels.size() : 1;
        const std::size_t nTime = times.empty() ? 1 : times.size();
        const std::size_t nMember = memberAxis >= 0 ? members.size() : 1;
        for (std::size_t mi = 0; mi < nMember; ++mi) {
            const int member = memberAxis >= 0 ? members[mi] : -1;
            for (std::size_t li = 0; li < nLev; ++li) {
                if (hasLevel && !levelUsable[li]) continue;
                const VerticalLevel level =
                    hasLevel ? levelAt(li) : VerticalLevel{VerticalLevel::Type::Surface, 0.0};
                for (std::size_t ti = 0; ti < nTime; ++ti) {
                    if (!times.empty() && !times[ti]) continue;
                    const core::RecordHandle handle =
                        (static_cast<core::RecordHandle>(mi) << kMemberShift) |
                        (static_cast<core::RecordHandle>(li) << kLevelShift) |
                        static_cast<core::RecordHandle>(ti);
                    catalog_.addRecord(nm, info.longName, info.units, info.standardName, level,
                                       times.empty() ? TimePoint{0} : *times[ti], member, handle);
                }
            }
        }
    }

    catalog_.finalize();
}

core::Field2D CfDataset::readField(const core::FieldKey& key) {
    // Process-wide: see the thread-safety note on the class.
    std::lock_guard<std::mutex> lock(libraryMutex());

    const auto handle = catalog_.resolve(key);
    if (!handle) throw ReadError("NetCDF: field not in catalog");
    const auto it = vars_.find(key.varName);
    if (it == vars_.end()) throw ReadError("NetCDF: unknown variable " + key.varName);
    const VarInfo& info = it->second;

    const std::size_t mi = static_cast<std::size_t>(*handle >> kMemberShift);
    const std::size_t li = static_cast<std::size_t>((*handle >> kLevelShift) & kIndexMask);
    const std::size_t ti = static_cast<std::size_t>(*handle & kIndexMask);

    // Open-time validation rejects anything but a regular lat/lon grid, so this
    // alternative always holds today. Checked rather than asserted via std::get
    // so that if that invariant ever weakens, the failure arrives as the
    // ReadError this interface documents instead of a bad_variant_access that
    // escapes every `catch (const ReadError&)` in the app.
    const auto* gridPtr = std::get_if<RegularLatLonGrid>(&grid_);
    if (!gridPtr) throw ReadError("NetCDF: field is not on a regular lat/lon grid");
    const std::size_t nlat = static_cast<std::size_t>(gridPtr->nlat);
    const std::size_t nlon = static_cast<std::size_t>(gridPtr->nlon);

    std::vector<std::size_t> start(static_cast<std::size_t>(info.ndims), 0);
    std::vector<std::size_t> count(static_cast<std::size_t>(info.ndims), 1);
    count[static_cast<std::size_t>(info.latAxis)] = nlat;
    count[static_cast<std::size_t>(info.lonAxis)] = nlon;
    if (info.timeAxis >= 0) start[static_cast<std::size_t>(info.timeAxis)] = ti;
    if (info.levelAxis >= 0) start[static_cast<std::size_t>(info.levelAxis)] = li;
    if (info.memberAxis >= 0) start[static_cast<std::size_t>(info.memberAxis)] = mi;

    // Read raw (unscaled) values; netcdf-c converts the storage type to double
    // but does NOT apply CF scale_factor/add_offset — we do that ourselves.
    std::vector<double> raw(nlat * nlon);
    if (nc_get_vara_double(ncid_, info.varid, start.data(), count.data(), raw.data()) != NC_NOERR)
        throw ReadError("NetCDF: slab read failed for " + key.varName);
    // The slab comes back in the variable's own axis order. A (…, lon, lat)
    // variable is therefore lon-major, and Field2D is row-major by latitude.
    if (info.lonAxis < info.latAxis) {
        std::vector<double> t(raw.size());
        for (std::size_t i = 0; i < nlon; ++i)
            for (std::size_t j = 0; j < nlat; ++j) t[j * nlon + i] = raw[i * nlat + j];
        raw.swap(t);
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    core::Field2D field;
    field.grid = grid_;
    field.values.resize(nlat * nlon);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        double v = raw[i];
        // Fill and missing values are compared as stored, before any _Unsigned
        // reinterpretation, since the attributes carry the stored type's bits.
        if (std::find(info.missing.begin(), info.missing.end(), v) != info.missing.end()) {
            field.values[i] = nan;
            continue;
        }
        if (info.unsignedBits > 0) v = asUnsigned(v, info.unsignedBits);
        if (v < info.validMin || v > info.validMax) {
            field.values[i] = nan;
            continue;
        }
        if (info.hasScale) v *= info.scale;
        if (info.hasOffset) v += info.offset;
        field.values[i] = static_cast<float>(v);
    }

    field.meta.varName = key.varName;
    field.meta.longName = info.longName;
    field.meta.units = info.units;
    field.meta.standardName = info.standardName;
    field.meta.level = key.level;
    field.meta.validTime = key.validTime;
    field.meta.member = key.member;
    return field;
}

}  // namespace met::readers::netcdf
