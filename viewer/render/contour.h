#pragma once

#include <vector>

#include "viewer/core/field.h"

namespace met::render {

// A contour line segment in fractional grid-index space (x = column, y = row).
// Callers project these to screen via the grid's indexToLatLon plus the view
// transform, so contours stay aligned with the rasterized field.
struct ContourSegment {
    double x0, y0, x1, y1;
};

// Marching-squares isolines of `field` at a single value. Cells touching a NaN
// corner are skipped. Crossing points are linearly interpolated along cell
// edges, so lines follow the field smoothly rather than stepping.
[[nodiscard]] std::vector<ContourSegment> contourAt(const core::Field2D& field, double level);

// Convenience: contours at every multiple of `interval` within the field's
// finite value range, returned grouped per level.
struct ContourLevel {
    double value;
    std::vector<ContourSegment> segments;
};
[[nodiscard]] std::vector<ContourLevel> contourLevels(const core::Field2D& field, double interval);

// Choose a "nice" contour interval (1/2/5 x 10^n) for a value range aiming for
// roughly `target` lines. Returns 0 if the range is degenerate.
[[nodiscard]] double niceContourInterval(double lo, double hi, int target);

// Memoizes contourLevels() for the last (field, interval) it was asked for. The
// segments are in grid-index space, so they are independent of the view transform
// and survive pan/zoom/resize; only a different field or interval rebuilds them.
// Views repaint on every mouse-move to follow the cursor readout, which would
// otherwise re-run marching squares over the whole grid per frame.
class ContourCache {
public:
    // `field` must outlive the returned reference (views hold it in a shared_ptr);
    // identity is compared by address, so a new field object always rebuilds.
    [[nodiscard]] const std::vector<ContourLevel>& levels(const core::Field2D& field,
                                                          double interval);
    void clear();

private:
    const core::Field2D* field_ = nullptr;
    double interval_ = 0.0;
    std::vector<ContourLevel> levels_;
};

}  // namespace met::render
