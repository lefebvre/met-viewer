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
// On a grid whose columns span the globe (RegularLatLonGrid::globalWrapLon) x may
// run up to nlon: the final cell interpolates from the last column back onto
// column 0, matching what the map warp draws across the seam.
[[nodiscard]] float sampleBilinearIndex(const core::Field2D& field, double x, double y);

}  // namespace met::analysis
