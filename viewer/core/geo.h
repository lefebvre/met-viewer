#pragma once

#include <algorithm>

namespace met::core {

// A geographic coordinate in degrees. Longitude convention is not fixed here;
// grids normalize as needed.
struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

// Axis-aligned geographic bounding box in degrees.
struct BBox {
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;

    [[nodiscard]] bool valid() const { return minLat <= maxLat && minLon <= maxLon; }
};

// Wrap a longitude into [-180, 180).
[[nodiscard]] inline double wrapLon180(double lon) {
    double x = lon;
    while (x >= 180.0) x -= 360.0;
    while (x < -180.0) x += 360.0;
    return x;
}

// Wrap a longitude into [0, 360).
[[nodiscard]] inline double wrapLon360(double lon) {
    double x = lon;
    while (x >= 360.0) x -= 360.0;
    while (x < 0.0) x += 360.0;
    return x;
}

// A fractional grid index (column x, row y) produced by GridDef::latlonToIndex.
// Out-of-domain results are flagged rather than clamped, so callers can drop
// samples that fall outside the grid.
struct GridIndex {
    double x = 0.0;
    double y = 0.0;
    bool inDomain = false;
};

}  // namespace met::core
