#pragma once

#include <QImage>

#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::render {

// Decide how a grid's native index order must be flipped to present it north-up
// (top row = highest latitude) and west-east (left column = lowest longitude),
// from the signed row/column spacing. Regular lat/lon and projected grids are
// both handled. Consumers that draw a field in flat index space (the raster and
// its contour overlay) must use the same flip so the two stay aligned.
void displayFlip(const core::GridDef& grid, bool& flipRows, bool& flipCols);

// Rasterize a field to an ARGB32 image using the colormap, one pixel per grid
// cell. The result is oriented north-up / west-east regardless of the grid's
// scan direction (rows/cols are flipped as needed from the signed spacing).
// Missing (NaN) cells take the colormap's NaN color.
[[nodiscard]] QImage fieldToImage(const core::Field2D& field, const Colormap& cmap);

}  // namespace met::render
