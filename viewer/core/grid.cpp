#include "viewer/core/grid.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

namespace {
// Relative tolerance so grids that are identical up to representational noise
// (e.g. the same domain re-derived by two files) still compare equal, while a
// genuinely different resolution/origin does not.
bool near(double a, double b) {
    const double scale = std::max({1.0, std::abs(a), std::abs(b)});
    return std::abs(a - b) <= 1e-6 * scale;
}
}  // namespace

bool sameGrid(const GridDef& a, const GridDef& b) {
    if (a.index() != b.index()) return false;
    if (const auto* ra = std::get_if<RegularLatLonGrid>(&a)) {
        const auto& rb = std::get<RegularLatLonGrid>(b);
        return ra->nlon == rb.nlon && ra->nlat == rb.nlat && near(ra->lat0, rb.lat0) &&
               near(ra->lon0, rb.lon0) && near(ra->dlat, rb.dlat) && near(ra->dlon, rb.dlon);
    }
    const auto& pa = std::get<ProjectedGrid>(a);
    const auto& pb = std::get<ProjectedGrid>(b);
    return pa.nx == pb.nx && pa.ny == pb.ny && near(pa.x0, pb.x0) && near(pa.y0, pb.y0) &&
           near(pa.dx, pb.dx) && near(pa.dy, pb.dy) && pa.crs.proj() == pb.crs.proj();
}

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
                if (!grid.crs.inverse(px, py, ll.lon, ll.lat)) {
                    const double nan = std::numeric_limits<double>::quiet_NaN();
                    return LatLon{nan, nan};  // let callers detect an invalid inverse
                }
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
                // A projected rectangle is not a lat/lon rectangle: sample the
                // border and take the geographic extent. Longitude uses a minimal
                // covering arc so a grid straddling the ±180° antimeridian gets a
                // correct (possibly >180°) span rather than a spurious near-global
                // one; an enclosed pole is detected explicitly since the border
                // never reaches an interior pole.
                std::vector<double> lons;
                double minLat = 90.0, maxLat = -90.0;
                const int steps = 32;
                auto acc = [&](double ix, double iy) {
                    const double px = grid.x0 + ix * grid.dx;
                    const double py = grid.y0 + iy * grid.dy;
                    double lon = 0, lat = 0;
                    if (!grid.crs.inverse(px, py, lon, lat)) return;
                    lons.push_back(lon);
                    minLat = std::min(minLat, lat);
                    maxLat = std::max(maxLat, lat);
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

                // Extend the latitude range to a pole that projects to an interior
                // grid point (a polar-stereographic grid centered on the pole).
                auto poleInside = [&](double poleLat) {
                    if (grid.dx == 0.0 || grid.dy == 0.0) return false;
                    double px = 0, py = 0;
                    if (!grid.crs.forward(0.0, poleLat, px, py)) return false;
                    const double gx = (px - grid.x0) / grid.dx;
                    const double gy = (py - grid.y0) / grid.dy;
                    return gx >= 0.0 && gx <= grid.nx - 1.0 && gy >= 0.0 && gy <= grid.ny - 1.0;
                };
                const bool northPole = poleInside(90.0);
                const bool southPole = poleInside(-90.0);
                if (northPole) maxLat = 90.0;
                if (southPole) minLat = -90.0;

                b.minLat = minLat;
                b.maxLat = maxLat;
                if (northPole || southPole || lons.empty()) {
                    b.minLon = -180.0;  // longitude is unconstrained around a pole
                    b.maxLon = 180.0;
                } else {
                    std::sort(lons.begin(), lons.end());
                    double gap = 0.0, gapStart = lons.front();
                    for (std::size_t i = 1; i < lons.size(); ++i) {
                        const double d = lons[i] - lons[i - 1];
                        if (d > gap) { gap = d; gapStart = lons[i - 1]; }
                    }
                    const double wrapGap = (lons.front() + 360.0) - lons.back();
                    if (wrapGap >= gap) {
                        b.minLon = lons.front();  // data contiguous within [-180,180]
                        b.maxLon = lons.back();
                    } else {
                        // The empty span is interior, so the covering arc crosses
                        // the seam: start just after the gap, wrap the earlier side.
                        b.minLon = gapStart + gap;
                        b.maxLon = gapStart + 360.0;
                    }
                }
            }
            return b;
        },
        g);
}

double gridSpacingDeg(const GridDef& g) {
    return std::visit(
        [](const auto& grid) -> double {
            using T = std::decay_t<decltype(grid)>;
            if constexpr (std::is_same_v<T, RegularLatLonGrid>) {
                return std::min(std::abs(grid.dlat), std::abs(grid.dlon));
            } else {
                // Projected spacing is in metres; ~111.32 km per degree of latitude
                // is close enough for a size hint at any usable latitude.
                constexpr double kMetresPerDeg = 111320.0;
                return std::min(std::abs(grid.dx), std::abs(grid.dy)) / kMetresPerDeg;
            }
        },
        g);
}

}  // namespace met::core
