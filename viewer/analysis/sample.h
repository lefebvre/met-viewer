#pragma once

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// Sample a field at a geographic point with NaN-aware bilinear interpolation.
// Returns NaN if the point is outside the grid or all four surrounding cells are
// missing. If some (but not all) corners are missing, falls back to the nearest
// valid corner.
[[nodiscard]] float sampleBilinear(const core::Field2D& field, core::LatLon at);

// Sample at a fractional grid index (column x, row y). Same NaN semantics.
[[nodiscard]] float sampleBilinearIndex(const core::Field2D& field, double x, double y);

}  // namespace met::analysis
