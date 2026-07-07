#include "viewer/readers/arl/arldataset.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <string>

#include <fmt/format.h>

#include "viewer/core/crs.h"

namespace met::readers::arl {
namespace {

using core::Field2D;
using core::GridDef;
using core::ProjectedGrid;
using core::RegularLatLonGrid;
using core::TimePoint;
using core::VerticalLevel;

// Fixed-width ASCII field parsers (ARL headers are Fortran-formatted text).
long fInt(const char* p, int off, int w) {
    std::string s(p + off, static_cast<std::size_t>(w));
    try {
        return std::stol(s);
    } catch (...) {
        return 0;
    }
}
double fDbl(const char* p, int off, int w) {
    std::string s(p + off, static_cast<std::size_t>(w));
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

struct Label {
    int iy, im, id, ih, ic, ll, kg;
    std::string kvar;
    int nexp;
    double prec, var1;
};

Label parseLabel(const char* p) {
    Label l;
    l.iy = static_cast<int>(fInt(p, 0, 2));
    l.im = static_cast<int>(fInt(p, 2, 2));
    l.id = static_cast<int>(fInt(p, 4, 2));
    l.ih = static_cast<int>(fInt(p, 6, 2));
    l.ic = static_cast<int>(fInt(p, 8, 2));
    l.ll = static_cast<int>(fInt(p, 10, 2));
    l.kg = static_cast<int>(fInt(p, 12, 2));
    l.kvar.assign(p + 14, 4);
    // trim trailing spaces
    while (!l.kvar.empty() && l.kvar.back() == ' ') l.kvar.pop_back();
    l.nexp = static_cast<int>(fInt(p, 18, 4));
    l.prec = fDbl(p, 22, 14);
    l.var1 = fDbl(p, 36, 14);
    return l;
}

TimePoint labelTime(const Label& l) {
    const int year = l.iy >= 48 ? 1900 + l.iy : 2000 + l.iy;
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = l.im - 1;
    tm.tm_mday = l.id;
    tm.tm_hour = l.ih;
    return TimePoint{static_cast<std::int64_t>(timegm(&tm))};
}

// Map an ARL 4-char variable name to (canonical name, units, long name).
struct VarMeta {
    std::string name, units, longName;
};
VarMeta mapVar(const std::string& kvar) {
    static const std::map<std::string, VarMeta> table = {
        {"PRSS", {"prss", "hPa", "Surface pressure"}},
        {"MSLP", {"mslp", "hPa", "Mean sea-level pressure"}},
        {"T02M", {"t2m", "K", "Temperature at 2 m"}},
        {"TEMP", {"t", "K", "Temperature"}},
        {"U10M", {"10u", "m/s", "U wind at 10 m"}},
        {"V10M", {"10v", "m/s", "V wind at 10 m"}},
        {"UWND", {"u", "m/s", "U wind"}},
        {"VWND", {"v", "m/s", "V wind"}},
        {"WWND", {"w", "hPa/s", "Vertical velocity"}},
        {"HGTS", {"gh", "gpm", "Geopotential height"}},
        {"RELH", {"r", "%", "Relative humidity"}},
        {"SPHU", {"q", "kg/kg", "Specific humidity"}},
        {"TPP6", {"tp6", "m", "6-hour precipitation"}},
        {"TPP3", {"tp3", "m", "3-hour precipitation"}},
        {"TPPA", {"tp", "m", "Accumulated precipitation"}},
    };
    const auto it = table.find(kvar);
    if (it != table.end()) return it->second;
    std::string lower = kvar;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return {lower, "", kvar};
}

// Build the grid from the parsed INDX floats. size_km == 0 => regular lat/lon;
// otherwise a conformal projection (Lambert / polar-stereo / Mercator).
GridDef buildGrid(const std::array<double, 12>& g, int nx, int ny) {
    const double pole_lat = g[0], pole_lon = g[1], ref_lat = g[2], ref_lon = g[3], size_km = g[4],
                 orient = g[5], tang_lat = g[6], sync_xp = g[7], sync_yp = g[8], sync_lat = g[9],
                 sync_lon = g[10];
    (void)pole_lat;
    (void)pole_lon;
    (void)orient;

    if (size_km == 0.0) {
        RegularLatLonGrid r;
        r.dlat = ref_lat;
        r.dlon = ref_lon;
        // Grid point (sync_xp, sync_yp) [1-based] is at (sync_lat, sync_lon).
        r.lat0 = sync_lat - (sync_yp - 1.0) * ref_lat;
        r.lon0 = sync_lon - (sync_xp - 1.0) * ref_lon;
        r.nlon = nx;
        r.nlat = ny;
        r.globalWrapLon = std::abs(ref_lon) * nx >= 359.0;
        return r;
    }

    // Conformal projection. R = 6371200 m (ARL sphere).
    constexpr double R = 6371200.0;
    std::string proj;
    if (std::abs(tang_lat) >= 89.9) {
        proj = fmt::format("+proj=stere +lat_0={} +lat_ts={} +lon_0={} +R={} +units=m +no_defs",
                           tang_lat > 0 ? 90.0 : -90.0, tang_lat, orient, R);
    } else if (std::abs(tang_lat) < 0.1) {
        proj = fmt::format("+proj=merc +lat_ts={} +lon_0={} +R={} +units=m +no_defs", ref_lat,
                           orient, R);
    } else {
        proj = fmt::format("+proj=lcc +lat_1={} +lat_2={} +lat_0={} +lon_0={} +R={} +units=m +no_defs",
                           tang_lat, tang_lat, tang_lat, orient, R);
    }

    ProjectedGrid p;
    p.crs = core::Crs(proj);
    p.nx = nx;
    p.ny = ny;
    p.dx = size_km * 1000.0;
    p.dy = size_km * 1000.0;
    // Anchor: project the sync point's lat/lon, then back off to grid point (0,0).
    double sx = 0, sy = 0;
    if (!p.crs.forward(sync_lon, sync_lat, sx, sy)) throw ReadError("ARL: projection failed");
    p.x0 = sx - (sync_xp - 1.0) * p.dx;
    p.y0 = sy - (sync_yp - 1.0) * p.dy;
    return p;
}

// Unpack ARL 1-byte differences into row-major floats. Validated against real
// NOAA data (see arl-format-decoded memory): running row differences with a
// row-start re-anchor, first point = var1.
std::vector<float> unpack(const unsigned char* cpack, int nx, int ny, int nexp, double var1) {
    const double scale = std::pow(2.0, 7 - nexp);
    std::vector<float> out(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    double rold = var1;
    std::size_t k = 0;
    for (int j = 0; j < ny; ++j) {
        double rowsav = 0.0;
        for (int i = 0; i < nx; ++i) {
            const double rval = (static_cast<double>(cpack[k]) - 127.0) / scale + rold;
            if (i == 0) rowsav = rval;
            rold = rval;
            out[k] = static_cast<float>(rval);
            ++k;
        }
        rold = rowsav;
    }
    return out;
}

}  // namespace

ArlDataset::ArlDataset(std::filesystem::path path) : path_(std::move(path)) { scan(); }

void ArlDataset::scan() {
    std::ifstream in(path_, std::ios::binary);
    if (!in) throw ReadError("ARL: cannot open " + path_.string());

    // Read the first label + INDX header.
    char label[50];
    if (!in.read(label, 50)) throw ReadError("ARL: file too short");
    Label first = parseLabel(label);
    if (first.kvar != "INDX") throw ReadError("ARL: first record is not INDX");

    // Read enough of the INDX data section for the fixed header + level table.
    std::string hdr(1500, '\0');
    in.read(hdr.data(), static_cast<std::streamsize>(hdr.size()));
    const char* p = hdr.data();

    std::array<double, 12> gf{};
    for (int k = 0; k < 12; ++k) gf[static_cast<std::size_t>(k)] = fDbl(p, 9 + 7 * k, 7);
    nx_ = static_cast<int>(fInt(p, 93, 3));
    ny_ = static_cast<int>(fInt(p, 96, 3));
    const int nz = static_cast<int>(fInt(p, 99, 3));
    grid_ = buildGrid(gf, nx_, ny_);
    recLen_ = static_cast<long>(nx_) * static_cast<long>(ny_) + 50;

    // Per-level heights (index 0 = surface). Used to assign pressure levels.
    std::vector<double> levelHeight(static_cast<std::size_t>(nz), 0.0);
    int off = 108;
    for (int l = 0; l < nz; ++l) {
        levelHeight[static_cast<std::size_t>(l)] = fDbl(p, off, 6);
        const int nvar = static_cast<int>(fInt(p, off + 6, 2));
        off += 8 + nvar * 8;
        if (off > static_cast<int>(hdr.size()) - 8) break;
    }

    // Scan every record's 50-byte label to build the catalog.
    in.clear();
    in.seekg(0, std::ios::end);
    const long fileSize = static_cast<long>(in.tellg());
    const long nrec = fileSize / recLen_;
    for (long r = 0; r < nrec; ++r) {
        const long base = r * recLen_;
        in.seekg(base, std::ios::beg);
        if (!in.read(label, 50)) break;
        Label lab = parseLabel(label);
        if (lab.kvar == "INDX") continue;

        VerticalLevel level;
        if (lab.ll <= 0) {
            level.type = VerticalLevel::Type::Surface;
        } else {
            const double h = lab.ll < nz ? levelHeight[static_cast<std::size_t>(lab.ll)] : lab.ll;
            level.type = VerticalLevel::Type::PressureHPa;
            level.value = h;
        }
        const VarMeta vm = mapVar(lab.kvar);
        catalog_.addRecord(vm.name, vm.longName, vm.units, "", level, labelTime(lab), -1,
                           static_cast<core::RecordHandle>(base));
    }
    catalog_.finalize();
}

Field2D ArlDataset::readField(const core::FieldKey& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto handle = catalog_.resolve(key);
    if (!handle) throw ReadError("ARL: field not in catalog");

    std::ifstream in(path_, std::ios::binary);
    if (!in) throw ReadError("ARL: cannot reopen " + path_.string());
    in.seekg(static_cast<std::streamoff>(*handle), std::ios::beg);

    char label[50];
    if (!in.read(label, 50)) throw ReadError("ARL: cannot read record label");
    const Label lab = parseLabel(label);

    std::vector<unsigned char> cpack(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_));
    if (!in.read(reinterpret_cast<char*>(cpack.data()), static_cast<std::streamsize>(cpack.size())))
        throw ReadError("ARL: cannot read packed data");

    Field2D field;
    field.grid = grid_;
    field.values = unpack(cpack.data(), nx_, ny_, lab.nexp, lab.var1);

    const VarMeta vm = mapVar(lab.kvar);
    field.meta.varName = vm.name;
    field.meta.longName = vm.longName;
    field.meta.units = vm.units;
    field.meta.level = key.level;
    field.meta.validTime = key.validTime;
    return field;
}

}  // namespace met::readers::arl
