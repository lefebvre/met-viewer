#include "viewer/render/fieldimage.h"

#include <variant>

namespace met::render {

void displayFlip(const core::GridDef& grid, bool& flipRows, bool& flipCols) {
    flipRows = false;
    flipCols = false;
    if (const auto* g = std::get_if<core::RegularLatLonGrid>(&grid)) {
        flipRows = g->dlat > 0.0;  // grid runs south->north; put north on top
        flipCols = g->dlon < 0.0;  // grid runs east->west; put west on left
    } else if (const auto* p = std::get_if<core::ProjectedGrid>(&grid)) {
        // Projected y increases toward the pole and x toward the east, so a
        // south-first / east-first scan must be flipped to draw north-up/west-left.
        flipRows = p->dy > 0.0;
        flipCols = p->dx < 0.0;
    }
}

QImage fieldToImage(const core::Field2D& field, const Colormap& cmap) {
    const int w = field.width();
    const int h = field.height();
    if (w <= 0 || h <= 0) return {};

    // Orient north-up / west-east regardless of the grid's native scan direction.
    bool flipRows = false, flipCols = false;
    displayFlip(field.grid, flipRows, flipCols);

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
