#pragma once

#include <QImage>

#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::render {

// Rasterize a field to an ARGB32 image using the colormap, one pixel per grid
// cell. The result is oriented north-up / west-east regardless of the grid's
// scan direction (rows/cols are flipped as needed from the signed spacing).
// Missing (NaN) cells take the colormap's NaN color.
[[nodiscard]] QImage fieldToImage(const core::Field2D& field, const Colormap& cmap);

}  // namespace met::render
