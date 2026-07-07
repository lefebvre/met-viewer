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
    // Fold into (-180, 180] around lon0 so nearest wrap is chosen.
    while (delta > 180.0) delta -= 360.0;
    while (delta <= -180.0) delta += 360.0;
    return delta;
}

}  // namespace

int gridWidth(const GridDef& g) {
    return std::visit([](const auto& grid) { return grid.nlon; }, g);
}

int gridHeight(const GridDef& g) {
    return std::visit([](const auto& grid) { return grid.nlat; }, g);
}

std::size_t gridCount(const GridDef& g) {
    return static_cast<std::size_t>(gridWidth(g)) * static_cast<std::size_t>(gridHeight(g));
}

GridIndex latlonToIndex(const GridDef& g, LatLon ll) {
    const auto& grid = std::get<RegularLatLonGrid>(g);
    GridIndex out;

    if (grid.dlon != 0.0) {
        const double dlon = relativeLon(grid, ll.lon);
        out.x = dlon / grid.dlon;
    }
    if (grid.dlat != 0.0) {
        out.y = (ll.lat - grid.lat0) / grid.dlat;
    }

    const double maxX = static_cast<double>(grid.nlon - 1);
    const double maxY = static_cast<double>(grid.nlat - 1);
    const bool xIn = grid.globalWrapLon ? true : (out.x >= 0.0 && out.x <= maxX);
    const bool yIn = out.y >= 0.0 && out.y <= maxY;
    out.inDomain = xIn && yIn;

    // For a global wrap grid, fold column index into [0, nlon).
    if (grid.globalWrapLon && grid.nlon > 0) {
        double n = static_cast<double>(grid.nlon);
        out.x = std::fmod(std::fmod(out.x, n) + n, n);
    }
    return out;
}

LatLon indexToLatLon(const GridDef& g, double x, double y) {
    const auto& grid = std::get<RegularLatLonGrid>(g);
    LatLon ll;
    ll.lon = grid.lon0 + x * grid.dlon;
    ll.lat = grid.lat0 + y * grid.dlat;
    return ll;
}

BBox gridBBox(const GridDef& g) {
    const auto& grid = std::get<RegularLatLonGrid>(g);
    const double latA = grid.lat0;
    const double latB = grid.lat0 + grid.dlat * (grid.nlat - 1);
    const double lonA = grid.lon0;
    const double lonB = grid.lon0 + grid.dlon * (grid.nlon - 1);
    BBox b;
    b.minLat = std::min(latA, latB);
    b.maxLat = std::max(latA, latB);
    b.minLon = std::min(lonA, lonB);
    b.maxLon = std::max(lonA, lonB);
    return b;
}

}  // namespace met::core
