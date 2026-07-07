#pragma once

#include <QImage>

#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::render {

// Describes the Web Mercator viewport to warp into: the world-pixel coordinate
// of the output image's top-left corner, the integer zoom, and the output size.
struct MercatorViewport {
    double topLeftWorldX = 0.0;
    double topLeftWorldY = 0.0;
    int zoom = 0;
    int width = 0;
    int height = 0;
};

// Warp a field into a Web Mercator raster matching `view`, colormapped, with the
// given global opacity (0..1) baked into the alpha. Pixels outside the field's
// domain are fully transparent. Returns an ARGB32_Premultiplied image.
//
// For regular lat/lon grids the inverse projection is closed-form and evaluated
// once per output row (latitude is constant along a Mercator row), so the inner
// loop is a bilinear gather + LUT lookup.
//
// `threads` <= 1 runs single-threaded; otherwise output rows are split across
// that many worker threads.
[[nodiscard]] QImage warpToMercator(const core::Field2D& field, const Colormap& cmap,
                                    const MercatorViewport& view, double opacity = 1.0,
                                    int threads = 1);

}  // namespace met::render
