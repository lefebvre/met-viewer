#pragma once

#include <functional>

#include <QString>

class QWidget;

namespace met::render {
class Colormap;
}

namespace met::app {

// What belongs on a figure's page.
//
// The canvas alone is not the figure. A view's colour scale lives in its control
// panel, beside the canvas rather than inside it (see addColormapControls), so a
// page carrying only the canvas would show a colourmapped field with nothing to
// read the colours against. The scale is therefore drawn onto the page from the
// view's own colormap rather than lifted off the screen, which also lets it run
// the full height of the canvas instead of whatever height the panel gave it.
struct Figure {
    QWidget* canvas = nullptr;
    const render::Colormap* colormap = nullptr;  // null for a view with no colour scale
    QString units;                               // suffix on the scale's labels
};

// Write `fig` to `path` as a PDF: vector, at the canvas's on-screen size, in the
// palette it is currently drawn with. Whatever the canvas paints as an image (the
// cross-section's and the 2D plot's fields are pre-rendered rasters) stays an image
// inside the PDF; everything drawn as painter primitives stays vector.
//
// The file is written through a QSaveFile, so a failure part-way leaves the old
// file alone rather than a truncated one. False on failure, with `error` set to why.
[[nodiscard]] bool exportFigureToPdf(const Figure& fig, const QString& path, QString* error);

// Give `canvas` a right-click "Export Figure…" menu.
//
// `figureFor` is asked for the figure at the moment the menu is used, not when it
// is installed: the colormap and its range move while a view is open, and the page
// has to carry what the canvas is showing right then. Failures go to `onError`,
// which is how the caller gets them in front of the user (a status bar, typically) —
// this has no view of its own to report into.
void installFigureExport(QWidget* canvas, const QString& stem, std::function<Figure()> figureFor,
                         std::function<void(const QString&)> onError);

}  // namespace met::app
