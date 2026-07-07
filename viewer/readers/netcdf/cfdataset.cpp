#include "viewer/readers/netcdf/cfdataset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>

#include <netcdf.h>

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
    if (nc_inq_attlen(ncid, varid, name, &len) != NC_NOERR) return std::nullopt;
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

bool isLatUnits(const std::string& u) {
    const std::string l = lower(u);
    return l == "degrees_north" || l == "degree_north" || l == "degrees_n" || l == "degreesn";
}
bool isLonUnits(const std::string& u) {
    const std::string l = lower(u);
    return l == "degrees_east" || l == "degree_east" || l == "degrees_e" || l == "degreese";
}

// Parse a CF time-unit string "<unit> since <YYYY-MM-DD[ hh:mm:ss]>". Returns
// (secondsPerUnit, baseEpochSeconds). Falls back to (1, 0) if unparseable.
std::pair<double, std::int64_t> parseTimeUnits(const std::string& units) {
    const std::string l = lower(units);
    const auto pos = l.find(" since ");
    double perUnit = 1.0;
    std::int64_t base = 0;
    if (pos == std::string::npos) return {perUnit, base};

    const std::string unitWord = l.substr(0, pos);
    if (unitWord.rfind("sec", 0) == 0) perUnit = 1.0;
    else if (unitWord.rfind("min", 0) == 0) perUnit = 60.0;
    else if (unitWord.rfind("hour", 0) == 0 || unitWord.rfind("hr", 0) == 0) perUnit = 3600.0;
    else if (unitWord.rfind("day", 0) == 0) perUnit = 86400.0;

    const std::string ref = units.substr(pos + 7);
    int y = 1970, mo = 1, d = 1, hh = 0, mm = 0, ss = 0;
    std::sscanf(ref.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss);
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    base = static_cast<std::int64_t>(timegm(&tm));
    return {perUnit, base};
}

}  // namespace

CfDataset::CfDataset(std::filesystem::path path) : path_(std::move(path)) {
    if (nc_open(path_.string().c_str(), NC_NOWRITE, &ncid_) != NC_NOERR)
        throw ReadError("NetCDF: cannot open " + path_.string());
    try {
        scan();
    } catch (...) {
        nc_close(ncid_);
        throw;
    }
}

CfDataset::~CfDataset() {
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
    int latDim = -1, lonDim = -1, timeDim = -1, levelDim = -1;
    int latVar = -1, lonVar = -1, timeVar = -1, levelVar = -1;

    auto classifyCoord = [&](int varid, const std::string& name, int dimid) {
        const std::string units = attText(ncid_, varid, "units").value_or("");
        const std::string std = lower(attText(ncid_, varid, "standard_name").value_or(""));
        const std::string ln = lower(name);
        if (latDim < 0 && (isLatUnits(units) || std == "latitude" || ln == "latitude" || ln == "lat")) {
            latDim = dimid; latVar = varid; return;
        }
        if (lonDim < 0 && (isLonUnits(units) || std == "longitude" || ln == "longitude" || ln == "lon")) {
            lonDim = dimid; lonVar = varid; return;
        }
        if (timeDim < 0 && (std == "time" || ln == "time" || lower(units).find(" since ") != std::string::npos)) {
            timeDim = dimid; timeVar = varid; return;
        }
        if (levelDim < 0) {
            const std::string lu = lower(units);
            const bool pressureUnit = lu == "hpa" || lu == "pa" || lu == "millibar" || lu == "mb" ||
                                      lu == "mbar";
            const bool pressureName = ln == "pressure_level" || ln == "plev" || ln == "level" ||
                                      ln == "lev" || ln == "isobaricinhpa" || ln == "isobaric";
            const bool pressureStd = std.find("air_pressure") != std::string::npos;
            if (pressureUnit || pressureName || pressureStd) {
                levelDim = dimid; levelVar = varid;
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

    if (latDim < 0 || lonDim < 0)
        throw ReadError("NetCDF: no latitude/longitude coordinates found");

    // Read coordinate arrays.
    std::vector<double> lat(dimLen[static_cast<std::size_t>(latDim)]);
    std::vector<double> lon(dimLen[static_cast<std::size_t>(lonDim)]);
    nc_get_var_double(ncid_, latVar, lat.data());
    nc_get_var_double(ncid_, lonVar, lon.data());

    std::vector<double> levels;
    VerticalLevel::Type levelType = VerticalLevel::Type::Surface;
    double levelScaleToHpa = 1.0;
    if (levelDim >= 0) {
        levels.resize(dimLen[static_cast<std::size_t>(levelDim)]);
        nc_get_var_double(ncid_, levelVar, levels.data());
        const std::string lu = lower(attText(ncid_, levelVar, "units").value_or("hpa"));
        levelType = VerticalLevel::Type::PressureHPa;
        levelScaleToHpa = (lu == "pa") ? 0.01 : 1.0;
    }

    std::vector<double> times;
    double timePerUnit = 1.0;
    std::int64_t timeBase = 0;
    if (timeDim >= 0) {
        times.resize(dimLen[static_cast<std::size_t>(timeDim)]);
        nc_get_var_double(ncid_, timeVar, times.data());
        const std::string tu = attText(ncid_, timeVar, "units").value_or("seconds since 1970-01-01");
        std::tie(timePerUnit, timeBase) = parseTimeUnits(tu);
    }

    // Build the grid geometry from the coordinate arrays (assumed regular).
    RegularLatLonGrid g;
    g.nlat = static_cast<int>(lat.size());
    g.nlon = static_cast<int>(lon.size());
    g.lat0 = lat.front();
    g.lon0 = lon.front();
    g.dlat = lat.size() > 1 ? lat[1] - lat[0] : -1.0;
    g.dlon = lon.size() > 1 ? lon[1] - lon[0] : 1.0;
    g.globalWrapLon = std::abs(g.dlon) * g.nlon >= 359.0;
    grid_ = g;

    auto levelAt = [&](std::size_t i) -> VerticalLevel {
        if (levels.empty()) return VerticalLevel{VerticalLevel::Type::Surface, 0.0};
        return VerticalLevel{levelType, levels[i] * levelScaleToHpa};
    };
    auto timeAt = [&](std::size_t i) -> TimePoint {
        if (times.empty()) return TimePoint{0};
        return TimePoint{timeBase + static_cast<std::int64_t>(std::llround(times[i] * timePerUnit))};
    };

    // Data variables: any non-coordinate var whose dims include lat and lon.
    for (int v = 0; v < nvars; ++v) {
        char nm[NC_MAX_NAME + 1] = {0};
        nc_type type;
        int vndims = 0, dimids[NC_MAX_VAR_DIMS] = {0}, vnatts = 0;
        nc_inq_var(ncid_, v, nm, &type, &vndims, dimids, &vnatts);
        if (v == latVar || v == lonVar || v == timeVar || v == levelVar) continue;

        int latAxis = -1, lonAxis = -1, timeAxis = -1, levelAxis = -1;
        for (int a = 0; a < vndims; ++a) {
            if (dimids[a] == latDim) latAxis = a;
            else if (dimids[a] == lonDim) lonAxis = a;
            else if (dimids[a] == timeDim) timeAxis = a;
            else if (dimids[a] == levelDim) levelAxis = a;
        }
        if (latAxis < 0 || lonAxis < 0) continue;  // not a griddable field

        VarInfo info;
        info.varid = v;
        info.ndims = vndims;
        info.latAxis = latAxis;
        info.lonAxis = lonAxis;
        info.timeAxis = timeAxis;
        info.levelAxis = levelAxis;
        info.units = attText(ncid_, v, "units").value_or("");
        info.longName = attText(ncid_, v, "long_name").value_or("");
        info.standardName = attText(ncid_, v, "standard_name").value_or("");
        if (auto s = attDouble(ncid_, v, "scale_factor")) { info.scale = *s; info.hasScale = true; }
        if (auto o = attDouble(ncid_, v, "add_offset")) { info.offset = *o; info.hasOffset = true; }
        if (auto f = attDouble(ncid_, v, "_FillValue")) { info.fill = *f; info.hasFill = true; }
        else if (auto f2 = attDouble(ncid_, v, "missing_value")) { info.fill = *f2; info.hasFill = true; }
        vars_[nm] = info;

        const std::size_t nLev = levels.empty() ? 1 : levels.size();
        const std::size_t nTime = times.empty() ? 1 : times.size();
        for (std::size_t li = 0; li < nLev; ++li) {
            for (std::size_t ti = 0; ti < nTime; ++ti) {
                const core::RecordHandle handle =
                    (static_cast<core::RecordHandle>(li) << 24) | static_cast<core::RecordHandle>(ti);
                catalog_.addRecord(nm, info.longName, info.units, info.standardName, levelAt(li),
                                   timeAt(ti), -1, handle);
            }
        }
    }

    catalog_.finalize();
}

core::Field2D CfDataset::readField(const core::FieldKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto handle = catalog_.resolve(key);
    if (!handle) throw ReadError("NetCDF: field not in catalog");
    const auto it = vars_.find(key.varName);
    if (it == vars_.end()) throw ReadError("NetCDF: unknown variable " + key.varName);
    const VarInfo& info = it->second;

    const std::size_t li = static_cast<std::size_t>(*handle >> 24);
    const std::size_t ti = static_cast<std::size_t>(*handle & 0xFFFFFF);

    const auto& grid = std::get<RegularLatLonGrid>(grid_);
    const std::size_t nlat = static_cast<std::size_t>(grid.nlat);
    const std::size_t nlon = static_cast<std::size_t>(grid.nlon);

    std::vector<std::size_t> start(static_cast<std::size_t>(info.ndims), 0);
    std::vector<std::size_t> count(static_cast<std::size_t>(info.ndims), 1);
    count[static_cast<std::size_t>(info.latAxis)] = nlat;
    count[static_cast<std::size_t>(info.lonAxis)] = nlon;
    if (info.timeAxis >= 0) start[static_cast<std::size_t>(info.timeAxis)] = ti;
    if (info.levelAxis >= 0) start[static_cast<std::size_t>(info.levelAxis)] = li;

    // Read raw (unscaled) values; netcdf-c converts the storage type to double
    // but does NOT apply CF scale_factor/add_offset — we do that ourselves.
    std::vector<double> raw(nlat * nlon);
    if (nc_get_vara_double(ncid_, info.varid, start.data(), count.data(), raw.data()) != NC_NOERR)
        throw ReadError("NetCDF: slab read failed for " + key.varName);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    core::Field2D field;
    field.grid = grid_;
    field.values.resize(nlat * nlon);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        double v = raw[i];
        if (info.hasFill && v == info.fill) {
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
    return field;
}

}  // namespace met::readers::netcdf
