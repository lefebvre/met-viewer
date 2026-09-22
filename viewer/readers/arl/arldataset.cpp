#include "viewer/readers/arl/arldataset.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "viewer/core/crs.h"
#include "viewer/core/timeaxis.h"

namespace met::readers::arl {
namespace {

using core::Field2D;
using core::GridDef;
using core::ProjectedGrid;
using core::RegularLatLonGrid;
using core::TimePoint;
using core::VerticalLevel;

// Fixed-width ASCII field parsers (ARL headers are Fortran-formatted text).
//
// These return nullopt rather than 0 on a bad parse, and callers are expected to
// turn that into a ReadError for any field they actually consume. Coercing to 0
// was the one quiet failure in this reader: `nexp` feeds the unpack scale and the
// date fields feed the time axis, so a silently zeroed field yields a
// plausible-looking grid full of wrong numbers instead of a refusal. That matters
// most for the exact bug this reader has already hit once — a misaligned record
// offset, where every field lands on the wrong bytes and the only signal that
// anything is wrong is that they stop parsing as numbers.
//
// A trailing-garbage check is part of that signal: Fortran writes these fields as
// digits padded with spaces, so anything else after the number means the offset,
// not just the value, is suspect.
[[nodiscard]] bool restIsBlank(const std::string& s, std::size_t pos) {
    return s.find_first_not_of(" \t", pos) == std::string::npos;
}

std::optional<long> fInt(const char* p, int off, int w) {
    const std::string s(p + off, static_cast<std::size_t>(w));
    try {
        std::size_t pos = 0;
        const long v = std::stol(s, &pos);
        if (!restIsBlank(s, pos)) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> fDbl(const char* p, int off, int w) {
    const std::string s(p + off, static_cast<std::size_t>(w));
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        if (!restIsBlank(s, pos)) return std::nullopt;
        if (!std::isfinite(v)) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

struct Label {
    int iy, im, id, ih, ic, ll, kg;
    std::string kvar;
    int nexp;
    double prec, var1;
};

// Returns nullopt if any field the decoder consumes is unparseable, or if the
// date fields are out of range. `ic`, `kg` and `prec` are read for completeness
// but nothing downstream uses them, so they stay tolerant — a blank there is not
// a reason to refuse a record whose data is fine.
std::optional<Label> parseLabel(const char* p) {
    const auto iy = fInt(p, 0, 2);
    const auto im = fInt(p, 2, 2);
    const auto id = fInt(p, 4, 2);
    const auto ih = fInt(p, 6, 2);
    const auto ll = fInt(p, 10, 2);
    const auto nexp = fInt(p, 18, 4);
    const auto var1 = fDbl(p, 36, 14);
    if (!iy || !im || !id || !ih || !ll || !nexp || !var1) return std::nullopt;

    // Range-check the calendar fields. timegmUtc would happily normalize month 47
    // into some other year, which is exactly the kind of plausible-looking wrong
    // answer this reader is trying not to produce.
    if (*iy < 0 || *iy > 99) return std::nullopt;
    if (*im < 1 || *im > 12) return std::nullopt;
    if (*id < 1 || *id > 31) return std::nullopt;
    if (*ih < 0 || *ih > 23) return std::nullopt;

    Label l;
    l.iy = static_cast<int>(*iy);
    l.im = static_cast<int>(*im);
    l.id = static_cast<int>(*id);
    l.ih = static_cast<int>(*ih);
    l.ll = static_cast<int>(*ll);
    l.nexp = static_cast<int>(*nexp);
    l.var1 = *var1;
    l.ic = static_cast<int>(fInt(p, 8, 2).value_or(0));
    l.kg = static_cast<int>(fInt(p, 12, 2).value_or(0));
    l.prec = fDbl(p, 22, 14).value_or(0.0);
    l.kvar.assign(p + 14, 4);
    // trim trailing spaces
    while (!l.kvar.empty() && l.kvar.back() == ' ') l.kvar.pop_back();
    return l;
}

TimePoint labelTime(const Label& l) {
    // ARL stores a two-digit year with no century. The split at 48 is a HYSPLIT-era
    // convention: the archive begins in 1948 (the NCEP/NCAR reanalysis epoch), so
    // 48-99 is 20th century and 00-47 is 21st. Data outside 1948-2047 cannot be
    // represented in this format at all — do not "fix" this by moving the pivot.
    const int year = l.iy >= 48 ? 1900 + l.iy : 2000 + l.iy;
    return TimePoint{core::timegmUtc(year, l.im, l.id, l.ih, 0, 0)};
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

    // Conformal projection. R = 6371200 m (ARL sphere). The central meridian is
    // REF_LON (the reference longitude, e.g. 262.5 = -97.5 for HRRR), NOT ORIENT
    // — ORIENT is the grid's rotation angle relative to the projection (0 for
    // every standard ARL grid and not expressible in these proj strings anyway).
    constexpr double R = 6371200.0;
    std::string proj;
    if (std::abs(tang_lat) >= 89.9) {
        proj = fmt::format("+proj=stere +lat_0={} +lat_ts={} +lon_0={} +R={} +units=m +no_defs",
                           tang_lat > 0 ? 90.0 : -90.0, tang_lat, ref_lon, R);
    } else if (std::abs(tang_lat) < 0.1) {
        proj = fmt::format("+proj=merc +lat_ts={} +lon_0={} +R={} +units=m +no_defs", ref_lat,
                           ref_lon, R);
    } else {
        proj =
            fmt::format("+proj=lcc +lat_1={} +lat_2={} +lat_0={} +lon_0={} +R={} +units=m +no_defs",
                        tang_lat, tang_lat, tang_lat, ref_lon, R);
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
    double scale = std::pow(2.0, 7 - nexp);
    if (!(scale > 0.0) || !std::isfinite(scale)) scale = 1.0;  // guard a garbage exponent
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

int decodeGridDim(int headerValue, unsigned char gridIdChar) {
    return gridIdChar >= 64 ? headerValue + (gridIdChar - 64) * 1000 : headerValue;
}

ArlDataset::ArlDataset(std::filesystem::path path) : path_(std::move(path)) { scan(); }

void ArlDataset::scan() {
    std::ifstream in(path_, std::ios::binary);
    if (!in) throw ReadError("ARL: cannot open " + path_.string());

    // Read the first label + INDX header.
    char label[50];
    if (!in.read(label, 50)) throw ReadError("ARL: file too short");
    const std::optional<Label> first = parseLabel(label);
    if (!first) throw ReadError("ARL: unparseable first record label");
    if (first->kvar != "INDX") throw ReadError("ARL: first record is not INDX");

    // Read a generous prefix of the INDX data section: enough for the fixed
    // header plus the variable-length level table. (A fixed 1500-byte buffer
    // truncated realistic multi-level files, leaving later level pressures zero.)
    std::string hdr(65536, '\0');
    in.read(hdr.data(), static_cast<std::streamsize>(hdr.size()));
    hdr.resize(static_cast<std::size_t>(in.gcount()));
    if (hdr.size() < 102) throw ReadError("ARL: INDX header too short");
    const char* p = hdr.data();

    // The 12 grid parameters. Indices 0, 1 and 5 (pole lat/lon and the grid
    // rotation angle) are not consumed by buildGrid, so they stay tolerant; the
    // rest define the projection and anchor and must parse.
    static constexpr std::array<bool, 12> kGridFieldRequired = {
        false, false, true, true, true, false, true, true, true, true, true, false};
    std::array<double, 12> gf{};
    for (int k = 0; k < 12; ++k) {
        const auto v = fDbl(p, 9 + 7 * k, 7);
        if (!v && kGridFieldRequired[static_cast<std::size_t>(k)])
            throw ReadError(fmt::format("ARL: unparseable INDX grid parameter {}", k));
        gf[static_cast<std::size_t>(k)] = v.value_or(0.0);
    }

    const auto nxField = fInt(p, 93, 3);
    const auto nyField = fInt(p, 96, 3);
    const auto nzField = fInt(p, 99, 3);
    if (!nxField || !nyField || !nzField) throw ReadError("ARL: unparseable INDX grid dimensions");
    nx_ = static_cast<int>(*nxField);
    ny_ = static_cast<int>(*nyField);
    const int nz = static_cast<int>(*nzField);
    // Grids larger than 999 in either dimension (e.g. HRRR at 1799x1059) store
    // only the low three digits in the I3 header field; the two grid-ID chars in
    // the label carry the thousands. Without this the record length is wrong and
    // every post-INDX record is read from a misaligned offset -> all-garbage data.
    nx_ = decodeGridDim(nx_, static_cast<unsigned char>(label[12]));
    ny_ = decodeGridDim(ny_, static_cast<unsigned char>(label[13]));
    // Range-check on top of the parse check above: a header that parses cleanly
    // can still be nonsense, and a degenerate dimension yields a bad record size.
    if (nx_ <= 0 || ny_ <= 0 || nz <= 0) throw ReadError("ARL: invalid INDX grid dimensions");
    grid_ = buildGrid(gf, nx_, ny_);
    recLen_ = static_cast<std::int64_t>(nx_) * static_cast<std::int64_t>(ny_) + 50;

    // Per-level heights (index 0 = surface). Used to assign level values.
    std::vector<double> levelHeight(static_cast<std::size_t>(nz), 0.0);
    int off = 108;
    for (int l = 0; l < nz; ++l) {
        if (off + 8 > static_cast<int>(hdr.size())) break;
        const auto height = fDbl(p, off, 6);
        // nvar walks the cursor to the next level entry, so a bad parse here does
        // not just lose one height — it desynchronizes the rest of the table and
        // every level below reads from the wrong offset.
        const auto nvar = fInt(p, off + 6, 2);
        if (!height || !nvar)
            throw ReadError(fmt::format("ARL: unparseable INDX level table at level {}", l));
        if (*nvar < 0) throw ReadError("ARL: negative variable count in INDX level table");
        levelHeight[static_cast<std::size_t>(l)] = *height;
        off += 8 + static_cast<int>(*nvar) * 8;
    }

    // Infer the vertical coordinate type from the level values. ARL stores
    // pressure (hPa), sigma (0..1), or height (m) depending on the source model;
    // choose by magnitude instead of assuming pressure everywhere.
    VerticalLevel::Type levelType = VerticalLevel::Type::PressureHPa;
    {
        double maxAbs = 0.0;
        bool anyUpper = false;
        for (int l = 1; l < nz; ++l) {
            maxAbs = std::max(maxAbs, std::abs(levelHeight[static_cast<std::size_t>(l)]));
            anyUpper = true;
        }
        if (anyUpper) {
            if (maxAbs <= 1.05) levelType = VerticalLevel::Type::Sigma;
            else if (maxAbs <= 1100.0) levelType = VerticalLevel::Type::PressureHPa;
            else levelType = VerticalLevel::Type::HeightM;
        }
    }

    // Scan every record's 50-byte label to build the catalog. Offsets are int64
    // throughout: a multi-day ARL meteorology file runs past 2 GB, and `long` is
    // 32-bit on the Windows build we ship an installer for.
    in.clear();
    in.seekg(0, std::ios::end);
    const std::int64_t fileSize = static_cast<std::int64_t>(in.tellg());
    const std::int64_t nrec = fileSize / recLen_;
    for (std::int64_t r = 0; r < nrec; ++r) {
        const std::int64_t base = r * recLen_;
        in.seekg(static_cast<std::streamoff>(base), std::ios::beg);
        if (!in.read(label, 50)) break;
        // Every record counted by nrec is a whole record, so a label that does not
        // parse here means the stride is wrong and the whole file is being read at
        // the wrong offsets — the failure mode the grid-ID decode above exists to
        // prevent. Refuse the file rather than cataloguing garbage records.
        const std::optional<Label> parsed = parseLabel(label);
        if (!parsed)
            throw ReadError(fmt::format(
                "ARL: unparseable record label at offset {} (record {} of {}); record stride "
                "{} is probably wrong",
                base, r, nrec, recLen_));
        const Label& lab = *parsed;
        if (lab.kvar == "INDX") continue;

        VerticalLevel level;
        if (lab.ll <= 0) {
            level.type = VerticalLevel::Type::Surface;
        } else {
            const double h = lab.ll < nz ? levelHeight[static_cast<std::size_t>(lab.ll)] : lab.ll;
            level.type = levelType;
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
    // nexp and var1 from this label set the unpack scale and the running-difference
    // seed, so decoding with zeroed stand-ins would produce a full field of wrong
    // values that looks entirely reasonable on a colormap.
    const std::optional<Label> parsed = parseLabel(label);
    if (!parsed)
        throw ReadError(fmt::format("ARL: unparseable record label at offset {}", *handle));
    const Label& lab = *parsed;

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
    field.meta.member = key.member;
    // ARL winds on a conformal grid are grid-relative; lat/lon are earth-relative.
    field.meta.gridRelativeWind = std::holds_alternative<ProjectedGrid>(grid_);
    return field;
}

}  // namespace met::readers::arl
