#include "viewer/readers/grib/gribdataset.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <eccodes.h>
#include <fmt/format.h>

#include "viewer/core/crs.h"

namespace met::readers::grib {
namespace {

using core::Field2D;
using core::FieldKey;
using core::GridDef;
using core::ProjectedGrid;
using core::RegularLatLonGrid;
using core::TimePoint;
using core::VerticalLevel;

// RAII for a codes_handle.
struct HandleGuard {
    codes_handle* h = nullptr;
    ~HandleGuard() {
        if (h) codes_handle_delete(h);
    }
};

// RAII for a C FILE*.
struct FileGuard {
    std::FILE* f = nullptr;
    ~FileGuard() {
        if (f) std::fclose(f);
    }
};

long getLong(codes_handle* h, const char* key) {
    long v = 0;
    if (codes_get_long(h, key, &v) != CODES_SUCCESS)
        throw ReadError(std::string("GRIB: missing key ") + key);
    return v;
}

double getDouble(codes_handle* h, const char* key) {
    double v = 0;
    if (codes_get_double(h, key, &v) != CODES_SUCCESS)
        throw ReadError(std::string("GRIB: missing key ") + key);
    return v;
}

std::string getString(codes_handle* h, const char* key) {
    char buf[512];
    std::size_t len = sizeof(buf);
    if (codes_get_string(h, key, buf, &len) != CODES_SUCCESS) return {};
    return std::string(buf);
}

bool hasKey(codes_handle* h, const char* key) { return codes_is_defined(h, key) != 0; }

VerticalLevel mapLevel(codes_handle* h) {
    const std::string type = getString(h, "typeOfLevel");
    const double level = hasKey(h, "level") ? getDouble(h, "level") : 0.0;
    VerticalLevel lvl;
    if (type == "isobaricInhPa") {
        lvl.type = VerticalLevel::Type::PressureHPa;
        lvl.value = level;
    } else if (type == "surface" || type == "meanSea" || type == "entireAtmosphere") {
        lvl.type = VerticalLevel::Type::Surface;
    } else if (type == "heightAboveGround" || type == "heightAboveSea") {
        lvl.type = VerticalLevel::Type::HeightM;
        lvl.value = level;
    } else if (type == "hybrid") {
        lvl.type = VerticalLevel::Type::Hybrid;
        lvl.value = level;
    } else if (type == "theta") {
        lvl.type = VerticalLevel::Type::Isentropic;
        lvl.value = level;
    } else {
        lvl.type = VerticalLevel::Type::Unknown;
        lvl.value = level;
    }
    return lvl;
}

TimePoint mapTime(codes_handle* h) {
    // validityDate = YYYYMMDD, validityTime = HHMM (may be < 100 for HH only).
    const long date = getLong(h, "validityDate");
    const long time = getLong(h, "validityTime");
    std::tm tm{};
    tm.tm_year = static_cast<int>(date / 10000) - 1900;
    tm.tm_mon = static_cast<int>((date / 100) % 100) - 1;
    tm.tm_mday = static_cast<int>(date % 100);
    tm.tm_hour = static_cast<int>(time / 100);
    tm.tm_min = static_cast<int>(time % 100);
    const std::time_t secs = timegm(&tm);
    return TimePoint{static_cast<std::int64_t>(secs)};
}

int memberOf(codes_handle* h) {
    // Only treat as an ensemble member for genuine ensemble products. A
    // deterministic GRIB1 analysis defines numberOfForecastsInEnsemble=0 and
    // perturbationNumber=0, which must map to the deterministic sentinel -1.
    if (hasKey(h, "numberOfForecastsInEnsemble") && getLong(h, "numberOfForecastsInEnsemble") > 0 &&
        hasKey(h, "perturbationNumber"))
        return static_cast<int>(getLong(h, "perturbationNumber"));
    return -1;
}

GridDef buildRegularLatLon(codes_handle* h) {
    RegularLatLonGrid g;
    g.nlon = static_cast<int>(getLong(h, "Ni"));
    g.nlat = static_cast<int>(getLong(h, "Nj"));
    g.lat0 = getDouble(h, "latitudeOfFirstGridPointInDegrees");
    g.lon0 = getDouble(h, "longitudeOfFirstGridPointInDegrees");

    const double iInc = getDouble(h, "iDirectionIncrementInDegrees");
    const double jInc = getDouble(h, "jDirectionIncrementInDegrees");
    const bool iNeg = getLong(h, "iScansNegatively") != 0;
    const bool jPos = getLong(h, "jScansPositively") != 0;

    // Signed spacing so index 0 is the first grid point and increasing index
    // follows the scan direction. This keeps ecCodes' native value order valid
    // without any reshuffling.
    g.dlon = iNeg ? -iInc : iInc;
    g.dlat = jPos ? jInc : -jInc;

    // Global longitude wrap when the columns span ~360 degrees.
    const double span = std::abs(iInc) * g.nlon;
    g.globalWrapLon = span >= 359.0;
    return g;
}

// Earth radius (metres) from shapeOfTheEarth, defaulting to the WMO sphere.
double earthRadius(codes_handle* h) {
    if (hasKey(h, "radius") && codes_is_defined(h, "radius")) {
        const double r = getDouble(h, "radius");
        if (r > 1e6) return r;
    }
    const long shape = hasKey(h, "shapeOfTheEarth") ? getLong(h, "shapeOfTheEarth") : 6;
    switch (shape) {
        case 0: return 6367470.0;
        case 6: return 6371229.0;
        case 8: return 6371200.0;
        default: return 6371229.0;
    }
}

// Build a ProjectedGrid, anchoring x0/y0 by projecting the first grid point.
GridDef buildProjected(codes_handle* h, const std::string& gridType) {
    const double R = earthRadius(h);
    const int nx = static_cast<int>(getLong(h, "Nx"));
    const int ny = static_cast<int>(getLong(h, "Ny"));
    const double lat1 = getDouble(h, "latitudeOfFirstGridPointInDegrees");
    const double lon1 = getDouble(h, "longitudeOfFirstGridPointInDegrees");
    const double dxm = getDouble(h, "DxInMetres");
    const double dym = getDouble(h, "DyInMetres");
    const bool iNeg = getLong(h, "iScansNegatively") != 0;
    const bool jPos = getLong(h, "jScansPositively") != 0;

    std::string proj;
    if (gridType == "lambert") {
        const double latin1 = getDouble(h, "Latin1InDegrees");
        const double latin2 = getDouble(h, "Latin2InDegrees");
        const double lad = getDouble(h, "LaDInDegrees");
        const double lov = getDouble(h, "LoVInDegrees");
        proj = fmt::format("+proj=lcc +lat_1={} +lat_2={} +lat_0={} +lon_0={} +R={} +units=m +no_defs",
                           latin1, latin2, lad, lov, R);
    } else {  // polar_stereographic
        const double lad = hasKey(h, "LaDInDegrees") ? getDouble(h, "LaDInDegrees") : 60.0;
        const double lov = getDouble(h, "orientationOfTheGridInDegrees");
        const bool south = hasKey(h, "projectionCentreFlag") &&
                           (getLong(h, "projectionCentreFlag") & 0x80) != 0;
        proj = fmt::format("+proj=stere +lat_0={} +lat_ts={} +lon_0={} +R={} +units=m +no_defs",
                           south ? -90.0 : 90.0, lad, lov, R);
    }

    ProjectedGrid g;
    g.crs = core::Crs(proj);
    g.nx = nx;
    g.ny = ny;
    g.dx = iNeg ? -dxm : dxm;
    g.dy = jPos ? dym : -dym;
    if (!g.crs.forward(lon1, lat1, g.x0, g.y0))
        throw ReadError("GRIB: cannot project first grid point");
    return g;
}

// Reject scan modes we do not normalize in M1.
void checkSupportedScan(codes_handle* h) {
    if (hasKey(h, "alternativeRowScanning") && getLong(h, "alternativeRowScanning") != 0)
        throw ReadError("GRIB: alternating row scanning not supported");
    if (hasKey(h, "jPointsAreConsecutive") && getLong(h, "jPointsAreConsecutive") != 0)
        throw ReadError("GRIB: j-consecutive point order not supported");
}

std::vector<float> readValues(codes_handle* h, std::size_t expected) {
    std::size_t len = 0;
    if (codes_get_size(h, "values", &len) != CODES_SUCCESS)
        throw ReadError("GRIB: cannot size values");
    if (len != expected)
        throw ReadError("GRIB: value count does not match grid dimensions");

    std::vector<double> raw(len);
    if (codes_get_double_array(h, "values", raw.data(), &len) != CODES_SUCCESS)
        throw ReadError("GRIB: cannot read values");

    const double missing = hasKey(h, "missingValue") ? getDouble(h, "missingValue") : 9999.0;
    const bool bitmap = hasKey(h, "bitmapPresent") && getLong(h, "bitmapPresent") != 0;
    const float nan = std::numeric_limits<float>::quiet_NaN();

    std::vector<float> out(len);
    for (std::size_t i = 0; i < len; ++i) {
        const double v = raw[i];
        out[i] = (bitmap && v == missing) ? nan : static_cast<float>(v);
    }
    return out;
}

}  // namespace

GribDataset::GribDataset(std::filesystem::path path) : path_(std::move(path)) { scan(); }

void GribDataset::scan() {
    FileGuard fg;
    fg.f = std::fopen(path_.string().c_str(), "rb");
    if (!fg.f) throw ReadError("GRIB: cannot open " + path_.string());

    int err = 0;
    while (true) {
        codes_handle* raw = codes_grib_handle_new_from_file(nullptr, fg.f, &err);
        if (!raw) break;
        HandleGuard hg{raw};
        if (err != CODES_SUCCESS) throw ReadError("GRIB: scan error");

        const std::string grid = getString(raw, "gridType");
        if (grid != "regular_ll" && grid != "lambert" && grid != "polar_stereographic")
            continue;  // supported grids: regular lat/lon + Lambert + polar-stereographic

        const auto offset = static_cast<core::RecordHandle>(getLong(raw, "offset"));
        catalog_.addRecord(getString(raw, "shortName"), getString(raw, "name"),
                           getString(raw, "units"), getString(raw, "cfName"), mapLevel(raw),
                           mapTime(raw), memberOf(raw), offset);
    }
    catalog_.finalize();
}

Field2D GribDataset::readField(const FieldKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto handle = catalog_.resolve(key);
    if (!handle) throw ReadError("GRIB: field not in catalog");

    FileGuard fg;
    fg.f = std::fopen(path_.string().c_str(), "rb");
    if (!fg.f) throw ReadError("GRIB: cannot reopen " + path_.string());
    if (std::fseek(fg.f, static_cast<long>(*handle), SEEK_SET) != 0)
        throw ReadError("GRIB: seek failed");

    int err = 0;
    codes_handle* raw = codes_grib_handle_new_from_file(nullptr, fg.f, &err);
    if (!raw || err != CODES_SUCCESS) throw ReadError("GRIB: cannot read message at offset");
    HandleGuard hg{raw};

    checkSupportedScan(raw);

    Field2D field;
    const std::string gridType = getString(raw, "gridType");
    if (gridType == "regular_ll")
        field.grid = buildRegularLatLon(raw);
    else
        field.grid = buildProjected(raw, gridType);
    field.values = readValues(raw, core::gridCount(field.grid));

    field.meta.varName = key.varName;
    field.meta.longName = getString(raw, "name");
    field.meta.units = getString(raw, "units");
    field.meta.standardName = getString(raw, "cfName");
    field.meta.level = mapLevel(raw);
    field.meta.validTime = mapTime(raw);
    // resolutionAndComponentFlags bit 3 (0x08): u/v are grid-relative.
    if (hasKey(raw, "resolutionAndComponentFlags"))
        field.meta.gridRelativeWind = (getLong(raw, "resolutionAndComponentFlags") & 0x08) != 0;
    return field;
}

}  // namespace met::readers::grib
