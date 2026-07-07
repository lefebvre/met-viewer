#include "viewer/core/grid.h"

#include <algorithm>
#include <cmath>

namespace met::core {
namespace {

// Bring `lon` as close as possible to the grid's column-0 longitude so that a
// point given in a different longitude convention (e.g. -170 vs 190) still maps
// onto the grid. Returns a longitude offset (in degrees) relative to lon0.
double relativeLon(const RegularLatLonGrid& g, double lon) {
    double delta = lon - g.lon0;
    while (delta > 180.0) delta -= 360.0;
    while (delta <= -180.0) delta += 360.0;
    return delta;
}

GridIndex latlonToIndexReg(const RegularLatLonGrid& grid, LatLon ll) {
    GridIndex out;
    if (grid.dlon != 0.0) out.x = relativeLon(grid, ll.lon) / grid.dlon;
    if (grid.dlat != 0.0) out.y = (ll.lat - grid.lat0) / grid.dlat;

    const double maxX = static_cast<double>(grid.nlon - 1);
    const double maxY = static_cast<double>(grid.nlat - 1);
    const bool xIn = grid.globalWrapLon ? true : (out.x >= 0.0 && out.x <= maxX);
    const bool yIn = out.y >= 0.0 && out.y <= maxY;
    out.inDomain = xIn && yIn;
    if (grid.globalWrapLon && grid.nlon > 0) {
        const double n = static_cast<double>(grid.nlon);
        out.x = std::fmod(std::fmod(out.x, n) + n, n);
    }
    return out;
}

GridIndex latlonToIndexProj(const ProjectedGrid& grid, LatLon ll) {
    GridIndex out;
    double x = 0, y = 0;
    if (!grid.crs.forward(ll.lon, ll.lat, x, y)) return out;  // inDomain=false
    if (grid.dx != 0.0) out.x = (x - grid.x0) / grid.dx;
    if (grid.dy != 0.0) out.y = (y - grid.y0) / grid.dy;
    // Small tolerance so a point projected back onto an exact grid line (which
    // may land at -1e-16 rather than 0) still counts as in-domain.
    constexpr double eps = 1e-6;
    out.inDomain = out.x >= -eps && out.x <= grid.nx - 1.0 + eps && out.y >= -eps &&
                   out.y <= grid.ny - 1.0 + eps;
    return out;
}

}  // namespace

int gridWidth(const GridDef& g) {
    return std::visit(
        [](const auto& grid) {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) return grid.nlon;
            else return grid.nx;
        },
        g);
}

int gridHeight(const GridDef& g) {
    return std::visit(
        [](const auto& grid) {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) return grid.nlat;
            else return grid.ny;
        },
        g);
}

std::size_t gridCount(const GridDef& g) {
    return static_cast<std::size_t>(gridWidth(g)) * static_cast<std::size_t>(gridHeight(g));
}

GridIndex latlonToIndex(const GridDef& g, LatLon ll) {
    return std::visit(
        [ll](const auto& grid) -> GridIndex {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) return latlonToIndexReg(grid, ll);
            else return latlonToIndexProj(grid, ll);
        },
        g);
}

LatLon indexToLatLon(const GridDef& g, double x, double y) {
    return std::visit(
        [x, y](const auto& grid) -> LatLon {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) {
                return {grid.lat0 + y * grid.dlat, grid.lon0 + x * grid.dlon};
            } else {
                const double px = grid.x0 + x * grid.dx;
                const double py = grid.y0 + y * grid.dy;
                LatLon ll;
                (void)grid.crs.inverse(px, py, ll.lon, ll.lat);
                return ll;
            }
        },
        g);
}

BBox gridBBox(const GridDef& g) {
    return std::visit(
        [](const auto& grid) -> BBox {
            using T = std::decay_t<decltype(grid)>;
            BBox b;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) {
                const double latA = grid.lat0;
                const double latB = grid.lat0 + grid.dlat * (grid.nlat - 1);
                const double lonA = grid.lon0;
                const double lonB = grid.lon0 + grid.dlon * (grid.nlon - 1);
                b.minLat = std::min(latA, latB);
                b.maxLat = std::max(latA, latB);
                b.minLon = std::min(lonA, lonB);
                b.maxLon = std::max(lonA, lonB);
            } else {
                // Sample the border of the projected grid and take the geographic
                // extent (a projected rectangle is not a lat/lon rectangle).
                b.minLat = 90;
                b.maxLat = -90;
                b.minLon = 180;
                b.maxLon = -180;
                const int steps = 32;
                auto acc = [&](double ix, double iy) {
                    const double px = grid.x0 + ix * grid.dx;
                    const double py = grid.y0 + iy * grid.dy;
                    double lon = 0, lat = 0;
                    if (!grid.crs.inverse(px, py, lon, lat)) return;
                    b.minLat = std::min(b.minLat, lat);
                    b.maxLat = std::max(b.maxLat, lat);
                    b.minLon = std::min(b.minLon, lon);
                    b.maxLon = std::max(b.maxLon, lon);
                };
                for (int k = 0; k <= steps; ++k) {
                    const double f = static_cast<double>(k) / steps;
                    const double ex = f * (grid.nx - 1);
                    const double ey = f * (grid.ny - 1);
                    acc(ex, 0);
                    acc(ex, grid.ny - 1);
                    acc(0, ey);
                    acc(grid.nx - 1, ey);
                }
            }
            return b;
        },
        g);
}

}  // namespace met::core
