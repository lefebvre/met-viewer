#pragma once

#include <algorithm>
#include <cmath>

#include "viewer/core/geo.h"

namespace met::render {

// Web Mercator (EPSG:3857) slippy-map math. "World pixels" are the global pixel
// coordinate at a given integer zoom: the world is (256 << zoom) pixels square,
// x increasing east from lon -180, y increasing south from the top (~85.05N).
namespace tile {

constexpr int kTileSize = 256;
// Web Mercator is clamped to +/- this latitude (where the projection diverges).
constexpr double kMaxLat = 85.05112877980659;

[[nodiscard]] inline double worldSize(int zoom) {
    return static_cast<double>(kTileSize) * static_cast<double>(1 << zoom);
}

[[nodiscard]] inline double clampLat(double lat) {
    return std::clamp(lat, -kMaxLat, kMaxLat);
}

// Longitude/latitude (degrees) -> world pixel coordinate at `zoom`.
[[nodiscard]] inline double lonToWorldX(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * worldSize(zoom);
}
[[nodiscard]] inline double latToWorldY(double lat, int zoom) {
    const double s = std::sin(clampLat(lat) * M_PI / 180.0);
    const double y = 0.5 - std::log((1.0 + s) / (1.0 - s)) / (4.0 * M_PI);
    return y * worldSize(zoom);
}

// World pixel -> longitude/latitude (degrees).
[[nodiscard]] inline double worldXToLon(double x, int zoom) {
    return x / worldSize(zoom) * 360.0 - 180.0;
}
[[nodiscard]] inline double worldYToLat(double y, int zoom) {
    const double n = M_PI * (1.0 - 2.0 * y / worldSize(zoom));
    return std::atan(std::sinh(n)) * 180.0 / M_PI;
}

[[nodiscard]] inline core::LatLon worldToLonLat(double x, double y, int zoom) {
    return {worldYToLat(y, zoom), worldXToLon(x, zoom)};
}

// Pick the integer zoom at which `spanDeg` longitude fits in `pixels`.
[[nodiscard]] inline int zoomForLonSpan(double spanDeg, int pixels, int maxZoom = 19) {
    if (spanDeg <= 0.0) return maxZoom;
    for (int z = maxZoom; z >= 0; --z) {
        const double px = spanDeg / 360.0 * worldSize(z);
        if (px <= pixels) return z;
    }
    return 0;
}

}  // namespace tile
}  // namespace met::render
