#include "viewer/analysis/wind.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>

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

float windSpeed(const WindField& w, double x, double y) {
    const UV uv = sampleWind(w, x, y);
    return std::sqrt(uv.u * uv.u + uv.v * uv.v);
}

double gridNorthAngle(const core::GridDef& grid, double i, double j) {
    // Regular lat/lon grids are already earth-relative.
    if (std::holds_alternative<core::RegularLatLonGrid>(grid)) return 0.0;

    // Sample the geographic direction of the grid's +y (row) axis by a small step.
    const int ny = core::gridHeight(grid);
    const double step = 0.25;
    const double j0 = std::min(j, static_cast<double>(ny - 1) - step);
    const core::LatLon a = core::indexToLatLon(grid, i, j0);
    const core::LatLon b = core::indexToLatLon(grid, i, j0 + step);

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
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const std::size_t k =
                static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
            const float ug = w.u.values[k];
            const float vg = w.v.values[k];
            if (std::isnan(ug) || std::isnan(vg)) continue;
            const double theta = gridNorthAngle(w.u.grid, i, j);
            const double c = std::cos(theta), s = std::sin(theta);
            // Grid-relative (along grid x/y) -> earth-relative (east/north).
            w.u.values[k] = static_cast<float>(ug * c + vg * s);
            w.v.values[k] = static_cast<float>(-ug * s + vg * c);
        }
    }
}

}  // namespace met::analysis
