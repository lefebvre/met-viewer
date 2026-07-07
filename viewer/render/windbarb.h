#pragma once

#include <vector>

#include <QLineF>
#include <QPointF>
#include <QPolygonF>

namespace met::render {

// Decomposition of a wind speed into standard barb elements.
struct BarbCount {
    int pennants = 0;  // 50 kt triangles
    int full = 0;      // 10 kt barbs
    int half = 0;      // 5 kt half-barbs
};

// Quantize a speed (knots) to pennants/full/half, rounding to the nearest 5 kt.
[[nodiscard]] BarbCount quantizeBarb(double speedKnots);

// Geometry for one wind barb, in screen coordinates (y-down).
struct WindBarb {
    std::vector<QLineF> lines;        // staff + barbs
    std::vector<QPolygonF> pennants;  // filled 50-kt triangles
    bool calm = false;                // draw a small circle instead
};

// Build a wind barb at `origin`. `fromDir` is a unit vector pointing in the
// direction the wind comes FROM (the staff extends along it). `length` is the
// staff length in pixels.
[[nodiscard]] WindBarb makeWindBarb(QPointF origin, QPointF fromDir, double speedKnots,
                                    double length);

}  // namespace met::render
