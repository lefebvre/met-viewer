#include "viewer/app/figureexport.h"

#include <memory>
#include <utility>

#include <QAction>
#include <QFileDialog>
#include <QMarginsF>
#include <QMenu>
#include <QObject>
#include <QPageSize>
#include <QPainter>
#include <QPalette>
#include <QPdfWriter>
#include <QPoint>
#include <QRegion>
#include <QSaveFile>
#include <QSizeF>
#include <QWidget>

#include "viewer/app/colorbarwidget.h"
#include "viewer/render/colormap.h"

namespace met::app {
namespace {
// Widget pixels are laid onto the page at this many per inch, so a 500 px canvas is
// always 375 pt wide. Taken as a constant rather than from the screen because the
// screen's logical DPI follows the user's display scaling: reading it there would
// make the same figure come out a different physical size on a different machine,
// while changing nothing about what is drawn.
constexpr double kPxPerInch = 96.0;
constexpr int kGapPx = 8;  // canvas-to-scale gap on the page

// The colour scale for `fig`, sized to run the full height of the canvas, or null
// when the view has no colormap.
std::unique_ptr<ColorbarWidget> scaleFor(const Figure& fig) {
    if (!fig.colormap) return nullptr;
    auto bar = std::make_unique<ColorbarWidget>();
    // The scale is drawn with the canvas's palette and font, not the application's:
    // the page is meant to match what the view looks like on screen, and the view
    // may be sitting under a theme the rest of the app is not.
    bar->setFont(fig.canvas->font());
    QPalette pal = fig.canvas->palette();
    // ColorbarWidget fills its background with Window, which is the control panel's
    // colour on screen. On the page it sits directly against the canvas, which is
    // filled with Base, so match that or the figure reads as two pasted-together
    // widgets rather than one.
    pal.setColor(QPalette::Window, fig.canvas->palette().color(QPalette::Base));
    bar->setPalette(pal);
    bar->setColormap(*fig.colormap);
    bar->setUnits(fig.units);
    bar->resize(bar->sizeHint().width(), fig.canvas->height());
    return bar;
}
}  // namespace

bool exportFigureToPdf(const Figure& fig, const QString& path, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (!fig.canvas || fig.canvas->width() < 1 || fig.canvas->height() < 1)
        return fail(QObject::tr("the view has nothing to draw"));

    const std::unique_ptr<ColorbarWidget> bar = scaleFor(fig);
    const int pageW = fig.canvas->width() + (bar ? kGapPx + bar->width() : 0);
    const int pageH = fig.canvas->height();

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    {
        QPdfWriter pdf(&file);
        pdf.setPageSize(QPageSize(QSizeF(pageW * 72.0 / kPxPerInch, pageH * 72.0 / kPxPerInch),
                                  QPageSize::Point, QString(), QPageSize::ExactMatch));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        pdf.setTitle(fig.canvas->windowTitle().isEmpty() ? QObject::tr("met-viewer figure")
                                                         : fig.canvas->windowTitle());

        QPainter p;
        if (!p.begin(&pdf)) return fail(QObject::tr("could not start a PDF page"));
        // The painter works in dots at the writer's resolution; the widgets draw in
        // their own pixels. One scale here puts both on the page at the size they
        // are on screen, and keeps the result vector rather than a scaled bitmap.
        const double scale = pdf.resolution() / kPxPerInch;
        p.scale(scale, scale);
        const auto flags = QWidget::DrawWindowBackground | QWidget::DrawChildren;
        fig.canvas->render(&p, QPoint(0, 0), QRegion(), flags);
        if (bar) bar->render(&p, QPoint(fig.canvas->width() + kGapPx, 0), QRegion(), flags);
        p.end();
    }
    // commit() is what makes the write visible; until then nothing has replaced an
    // existing file at `path`.
    if (!file.commit()) return fail(file.errorString());
    return true;
}

void installFigureExport(QWidget* canvas, std::function<QString()> stemFor,
                         std::function<Figure()> figureFor,
                         std::function<void(const QString&)> onError) {
    canvas->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(canvas, &QWidget::customContextMenuRequested, canvas,
                     [canvas, stemFor = std::move(stemFor), figureFor = std::move(figureFor),
                      onError = std::move(onError)](const QPoint& pos) {
                         QMenu menu(canvas);
                         QAction* act = menu.addAction(QObject::tr("Export Figure…"));
                         if (menu.exec(canvas->mapToGlobal(pos)) != act) return;
                         const QString path = QFileDialog::getSaveFileName(
                             canvas, QObject::tr("Export figure"),
                             stemFor() + QStringLiteral(".pdf"),
                             QObject::tr("PDF documents (*.pdf);;All files (*)"));
                         if (path.isEmpty()) return;
                         QString error;
                         if (!exportFigureToPdf(figureFor(), path, &error))
                             onError(QObject::tr("Could not write %1: %2").arg(path, error));
                     });
}

}  // namespace met::app
