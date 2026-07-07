#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

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

// Mean Earth radius in kilometres (spherical approximation).
inline constexpr double kEarthRadiusKm = 6371.0088;

// Great-circle distance (km) between two points.
[[nodiscard]] inline double greatCircleKm(LatLon a, LatLon b) {
    const double d2r = M_PI / 180.0;
    const double la1 = a.lat * d2r, la2 = b.lat * d2r;
    const double dla = (b.lat - a.lat) * d2r, dlo = (b.lon - a.lon) * d2r;
    const double h = std::sin(dla / 2) * std::sin(dla / 2) +
                     std::cos(la1) * std::cos(la2) * std::sin(dlo / 2) * std::sin(dlo / 2);
    return 2.0 * kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(h)));
}

// Interpolate a point a fraction t in [0,1] along the great-circle arc a->b.
[[nodiscard]] inline LatLon slerp(LatLon a, LatLon b, double t) {
    const double d2r = M_PI / 180.0, r2d = 180.0 / M_PI;
    const double la1 = a.lat * d2r, lo1 = a.lon * d2r, la2 = b.lat * d2r, lo2 = b.lon * d2r;
    const double d = greatCircleKm(a, b) / kEarthRadiusKm;  // angular distance
    if (d < 1e-9) return a;
    const double sd = std::sin(d);
    const double A = std::sin((1 - t) * d) / sd;
    const double B = std::sin(t * d) / sd;
    const double x = A * std::cos(la1) * std::cos(lo1) + B * std::cos(la2) * std::cos(lo2);
    const double y = A * std::cos(la1) * std::sin(lo1) + B * std::cos(la2) * std::sin(lo2);
    const double z = A * std::sin(la1) + B * std::sin(la2);
    LatLon out;
    out.lat = std::atan2(z, std::sqrt(x * x + y * y)) * r2d;
    out.lon = std::atan2(y, x) * r2d;
    return out;
}

// Sample a polyline path (in lat/lon) into `n` evenly-spaced points by
// cumulative great-circle distance. Returns the points and their distances (km).
struct SampledPath {
    std::vector<LatLon> points;
    std::vector<double> distancesKm;
};
[[nodiscard]] inline SampledPath sampleGreatCirclePath(const std::vector<LatLon>& vertices, int n) {
    SampledPath out;
    if (vertices.size() < 2 || n < 2) {
        if (!vertices.empty()) {
            out.points.push_back(vertices.front());
            out.distancesKm.push_back(0.0);
        }
        return out;
    }
    // Cumulative distance at each vertex.
    std::vector<double> cum(vertices.size(), 0.0);
    for (std::size_t i = 1; i < vertices.size(); ++i)
        cum[i] = cum[i - 1] + greatCircleKm(vertices[i - 1], vertices[i]);
    const double total = cum.back();

    for (int k = 0; k < n; ++k) {
        const double target = total * k / (n - 1);
        // Find the segment containing `target`.
        std::size_t seg = 1;
        while (seg + 1 < vertices.size() && cum[seg] < target) ++seg;
        const double segLen = cum[seg] - cum[seg - 1];
        const double t = segLen > 1e-9 ? (target - cum[seg - 1]) / segLen : 0.0;
        out.points.push_back(slerp(vertices[seg - 1], vertices[seg], t));
        out.distancesKm.push_back(target);
    }
    return out;
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
