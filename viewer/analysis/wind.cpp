#include "viewer/analysis/wind.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <variant>
#include <vector>

#include "viewer/analysis/sample.h"

namespace met::analysis {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Ordered list of canonical (u, v) name pairs to look for.
const std::array<WindPair, 6>& pairTable() {
    static const std::array<WindPair, 6> t = {{
        {"u", "v"},
        {"10u", "10v"},
        {"100u", "100v"},
        {"ugrd", "vgrd"},
        {"uwnd", "vwnd"},
        {"eastward_wind", "northward_wind"},
    }};
    return t;
}

}  // namespace

std::optional<WindPair> findWindPair(const std::vector<std::string>& varNames) {
    std::vector<std::string> lc;
    lc.reserve(varNames.size());
    for (const auto& n : varNames) lc.push_back(lower(n));
    auto has = [&](const std::string& n) {
        return std::find(lc.begin(), lc.end(), n) != lc.end();
    };
    for (const auto& p : pairTable()) {
        if (has(p.uName) && has(p.vName)) {
            // Return the original-cased names.
            WindPair out;
            for (const auto& n : varNames) {
                if (lower(n) == p.uName) out.uName = n;
                if (lower(n) == p.vName) out.vName = n;
            }
            return out;
        }
    }
    return std::nullopt;
}

UV sampleWind(const WindField& w, double x, double y) {
    return {sampleBilinearIndex(w.u, x, y), sampleBilinearIndex(w.v, x, y)};
}

UV sampleWindLatLon(const WindField& w, core::LatLon at) {
    const core::GridIndex gi = core::latlonToIndex(w.u.grid, at);
    if (!gi.inDomain) return {std::nanf(""), std::nanf("")};
    return sampleWind(w, gi.x, gi.y);
}

UV earthRelativeWindAt(const core::Field2D& uGrid, const core::Field2D& vGrid, core::LatLon at) {
    const core::GridIndex gi = core::latlonToIndex(uGrid.grid, at);
    if (!gi.inDomain) return {std::nanf(""), std::nanf("")};
    const float ug = sampleBilinearIndex(uGrid, gi.x, gi.y);
    const float vg = sampleBilinearIndex(vGrid, gi.x, gi.y);
    // Already earth-relative (regular lat/lon, or non-grid-relative components):
    // no rotation needed. Also short-circuits when either sample is NaN.
    if (std::isnan(ug) || std::isnan(vg) || !uGrid.meta.gridRelativeWind ||
        std::holds_alternative<core::RegularLatLonGrid>(uGrid.grid))
        return {ug, vg};
    // Rotate just this vector by the meridian convergence at the sample point —
    // identical math to rotateToEarthRelative(), evaluated once instead of per cell.
    const double theta = gridNorthAngle(uGrid.grid, gi.x, gi.y);
    const double c = std::cos(theta), s = std::sin(theta);
    return {static_cast<float>(ug * c + vg * s), static_cast<float>(-ug * s + vg * c)};
}

float windSpeed(const WindField& w, double x, double y) {
    const UV uv = sampleWind(w, x, y);
    return std::sqrt(uv.u * uv.u + uv.v * uv.v);
}

double gridNorthAngle(const core::GridDef& grid, double i, double j) {
    // Regular lat/lon grids are already earth-relative.
    if (std::holds_alternative<core::RegularLatLonGrid>(grid)) return 0.0;

    const auto& p = std::get<core::ProjectedGrid>(grid);
    const int ny = core::gridHeight(grid);
    if (ny < 2) return 0.0;

    // Sample the geographic direction of the PROJECTION's +y axis, which is what
    // grid-relative u/v are resolved against — not the direction of increasing row
    // index. The two differ whenever the source scans north-to-south (dy < 0, i.e.
    // GRIB jScansPositively = 0): stepping +j would then measure the bearing of
    // projected -y, giving an angle 180 degrees off and flipping both components.
    const double sign = p.dy < 0.0 ? -1.0 : 1.0;
    const double step = 0.25 * sign;
    // Keep both sample points inside the grid: j0 and j0+step must lie in [0, ny-1].
    const double lo = step > 0.0 ? 0.0 : -step;
    const double hi = step > 0.0 ? (ny - 1.0) - step : ny - 1.0;
    const double j0 = std::clamp(j, lo, hi);
    const core::LatLon a = core::indexToLatLon(grid, i, j0);
    const core::LatLon b = core::indexToLatLon(grid, i, j0 + step);
    if (std::isnan(a.lat) || std::isnan(b.lat)) return 0.0;  // inverse failed

    double dlon = b.lon - a.lon;
    while (dlon > 180.0) dlon -= 360.0;
    while (dlon < -180.0) dlon += 360.0;
    const double east = dlon * std::cos(a.lat * std::numbers::pi / 180.0);
    const double north = b.lat - a.lat;
    // Bearing of grid-north east of true north.
    return std::atan2(east, north);
}

void rotateToEarthRelative(WindField& w) {
    // Only conformal grids with grid-relative components need rotation.
    if (!w.u.meta.gridRelativeWind) return;
    if (std::holds_alternative<core::RegularLatLonGrid>(w.u.grid)) return;

    const int nx = w.width();
    const int ny = w.height();
    if (nx <= 0 || ny <= 0) return;

    // Rotate (ug, vg) by theta: grid-relative (along projected x/y) -> earth-
    // relative (east/north). Takes cos/sin so the caller can supply an
    // interpolated direction without an atan2 round-trip.
    auto rotateCell = [&w, nx](int i, int j, double c, double s) {
        const std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                              static_cast<std::size_t>(i);
        const float ug = w.u.values[k];
        const float vg = w.v.values[k];
        if (std::isnan(ug) || std::isnan(vg)) return;
        w.u.values[k] = static_cast<float>(ug * c + vg * s);
        w.v.values[k] = static_cast<float>(-ug * s + vg * c);
    };

    if (nx < 2 || ny < 2) {  // too small to interpolate over; evaluate directly
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double t = gridNorthAngle(w.u.grid, i, j);
                rotateCell(i, j, std::cos(t), std::sin(t));
            }
        return;
    }

    // Meridian convergence varies smoothly and very nearly linearly across a
    // conformal projection (for Lambert it is exactly n*(lon - lon0)), so evaluate
    // it on a coarse lattice and bilinearly interpolate between nodes. Each node
    // costs two PROJ inverse transforms; doing that per cell instead costs
    // 2*nx*ny of them — ~340 ms on an HRRR-sized grid, for an answer that differs
    // by well under a hundredth of a degree.
    constexpr int kStep = 16;  // cells between lattice nodes
    const int lw = (nx - 2) / kStep + 2;
    const int lh = (ny - 2) / kStep + 2;
    const double spanX = (nx - 1.0) / (lw - 1);  // cells per lattice interval
    const double spanY = (ny - 1.0) / (lh - 1);

    std::vector<double> cosT(static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
    std::vector<double> sinT(cosT.size());
    for (int lj = 0; lj < lh; ++lj) {
        for (int li = 0; li < lw; ++li) {
            const double t = gridNorthAngle(w.u.grid, li * spanX, lj * spanY);
            const std::size_t n =
                static_cast<std::size_t>(lj) * static_cast<std::size_t>(lw) +
                static_cast<std::size_t>(li);
            cosT[n] = std::cos(t);
            sinT[n] = std::sin(t);
        }
    }

    for (int j = 0; j < ny; ++j) {
        const double fy = j / spanY;
        const int lj = std::min(static_cast<int>(fy), lh - 2);
        const double ty = fy - lj;
        for (int i = 0; i < nx; ++i) {
            const double fx = i / spanX;
            const int li = std::min(static_cast<int>(fx), lw - 2);
            const double tx = fx - li;
            const std::size_t n00 = static_cast<std::size_t>(lj) * static_cast<std::size_t>(lw) +
                                    static_cast<std::size_t>(li);
            const std::size_t n10 = n00 + 1;
            const std::size_t n01 = n00 + static_cast<std::size_t>(lw);
            const std::size_t n11 = n01 + 1;
            const double w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty);
            const double w01 = (1 - tx) * ty, w11 = tx * ty;
            // Interpolate the direction vector, not the angle: blending cos/sin
            // avoids the wrap discontinuity at +/-pi.
            double c = cosT[n00] * w00 + cosT[n10] * w10 + cosT[n01] * w01 + cosT[n11] * w11;
            double s = sinT[n00] * w00 + sinT[n10] * w10 + sinT[n01] * w01 + sinT[n11] * w11;
            const double len = std::hypot(c, s);
            if (len > 1e-12) { c /= len; s /= len; }  // renormalize to a rotation
            rotateCell(i, j, c, s);
        }
    }
    // The values changed in place, so anything caching a derived product against
    // the field id (rasters, isolines, GPU textures) must see a new field.
    w.u.id = core::nextFieldId();
    w.v.id = core::nextFieldId();
}

}  // namespace met::analysis
