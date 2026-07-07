#include "viewer/render/fieldimage.h"

#include <variant>

namespace met::render {

QImage fieldToImage(const core::Field2D& field, const Colormap& cmap) {
    const int w = field.width();
    const int h = field.height();
    if (w <= 0 || h <= 0) return {};

    // Determine orientation from the grid's signed spacing so the output is
    // always north-up (top row = highest latitude) and west-east (left = lowest
    // longitude).
    const auto& grid = std::get<core::RegularLatLonGrid>(field.grid);
    const bool flipRows = grid.dlat > 0.0;  // grid runs south->north; put north on top
    const bool flipCols = grid.dlon < 0.0;  // grid runs east->west; put west on left

    QImage img(w, h, QImage::Format_ARGB32);
    for (int r = 0; r < h; ++r) {
        const int gr = flipRows ? (h - 1 - r) : r;
        auto* scan = reinterpret_cast<QRgb*>(img.scanLine(r));
        for (int c = 0; c < w; ++c) {
            const int gc = flipCols ? (w - 1 - c) : c;
            const Rgba px = cmap.map(field.at(gc, gr));
            scan[c] = qRgba(px.r, px.g, px.b, px.a);
        }
    }
    return img;
}

}  // namespace met::render
