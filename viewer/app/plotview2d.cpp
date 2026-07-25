#include "viewer/app/plotview2d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <variant>

#include <QMouseEvent>
#include <QPainter>

#include "viewer/analysis/sample.h"
#include "viewer/analysis/wind.h"
#include "viewer/app/hoverreadout.h"
#include "viewer/render/contour.h"
#include "viewer/render/fieldimage.h"
#include "viewer/render/windbarb.h"

namespace met::app {
namespace {
constexpr int kMarginLeft = 56;
constexpr int kMarginBottom = 34;
constexpr int kMarginTop = 12;
constexpr int kMarginRight = 16;

// Choose a "nice" tick step for an axis spanning `range` into ~`target` ticks.
double niceStep(double range, int target) {
    if (range <= 0 || target <= 0) return 1.0;
    const double raw = range / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double step = 10.0;
    if (norm < 1.5) step = 1.0;
    else if (norm < 3.0) step = 2.0;
    else if (norm < 7.0) step = 5.0;
    return step * mag;
}
}  // namespace

PlotView2D::PlotView2D(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    cmap_.setRange(0.0, 1.0);
    connect(&HoverOptions::instance(), &HoverOptions::changed, this, [this](HoverView v) {
        if (v != HoverView::Plot) return;
        hoverActive_ = false;  // drop a badge left over from before the toggle
        update();
    });
}

void PlotView2D::setColormapByName(const QString& name) {
    const double lo = cmap_.min();
    const double hi = cmap_.max();
    cmap_ = render::Colormap::builtin(name.toStdString());
    cmap_.setRange(lo, hi);
    if (autoRange_ && field_) autorange();  // re-center if the new map is diverging
    rebuildImage();
    update();
}

void PlotView2D::setField(std::shared_ptr<core::Field2D> field) {
    field_ = std::move(field);
    if (field_) {
        bbox_ = core::gridBBox(field_->grid);
        if (autoRange_) autorange();
        rebuildImage();
    } else {
        image_ = {};
    }
    update();
}

void PlotView2D::setAutoRange(bool on) {
    autoRange_ = on;
    if (on && field_) {
        autorange();
        rebuildImage();
        update();
    }
}

void PlotView2D::setRange(double lo, double hi) {
    cmap_.setRange(lo, hi);
    rebuildImage();
    update();
    emit rangeChanged(lo, hi);
}

void PlotView2D::clearField() {
    field_.reset();
    image_ = {};
    update();
}

void PlotView2D::setContoursEnabled(bool enabled) {
    contoursEnabled_ = enabled;
    update();
}

void PlotView2D::setContourInterval(double interval) {
    contourInterval_ = interval;
    update();
}

void PlotView2D::setWind(std::shared_ptr<analysis::WindField> wind) {
    wind_ = std::move(wind);
    update();
}

void PlotView2D::setWindMode(int mode) {
    windMode_ = mode;
    update();
}

bool PlotView2D::isLatLonGrid() const {
    return field_ && std::holds_alternative<core::RegularLatLonGrid>(field_->grid);
}

QPointF PlotView2D::indexToScreen(double col, double row, const QRectF& r) const {
    // Flat index-space mapping matching render::fieldToImage, so contour lines
    // (expressed in grid-index coordinates) stay aligned with the raster rather
    // than being warped through the map projection.
    const int w = core::gridWidth(field_->grid);
    const int h = core::gridHeight(field_->grid);
    bool flipRows = false, flipCols = false;
    render::displayFlip(field_->grid, flipRows, flipCols);
    double fx = w > 1 ? col / (w - 1) : 0.0;
    double fy = h > 1 ? row / (h - 1) : 0.0;
    if (flipCols) fx = 1.0 - fx;
    if (flipRows) fy = 1.0 - fy;
    return {r.left() + fx * r.width(), r.top() + fy * r.height()};
}

bool PlotView2D::screenToIndex(QPointF pos, const QRectF& r, double& col, double& row) const {
    if (!field_ || r.width() <= 0 || r.height() <= 0) return false;
    const int w = core::gridWidth(field_->grid);
    const int h = core::gridHeight(field_->grid);
    if (w <= 0 || h <= 0) return false;
    bool flipRows = false, flipCols = false;
    render::displayFlip(field_->grid, flipRows, flipCols);
    double fx = (pos.x() - r.left()) / r.width();
    double fy = (pos.y() - r.top()) / r.height();
    if (flipCols) fx = 1.0 - fx;
    if (flipRows) fy = 1.0 - fy;
    if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0) return false;
    col = fx * (w - 1);
    row = fy * (h - 1);
    return true;
}

void PlotView2D::autorange() {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (float v : field_->values) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, static_cast<double>(v));
        hi = std::max(hi, static_cast<double>(v));
    }
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi) {
        lo = 0.0;
        hi = 1.0;
    }
    // Diverging colormaps read best centered on zero.
    if (render::Colormap::isDiverging(cmap_.name())) {
        const double m = std::max(std::abs(lo), std::abs(hi));
        lo = -m;
        hi = m;
    }
    cmap_.setRange(lo, hi);
    emit rangeChanged(lo, hi);
}

void PlotView2D::rebuildImage() {
    if (field_) image_ = render::fieldToImage(*field_, cmap_);
}

QRectF PlotView2D::plotRect() const {
    QRectF full(kMarginLeft, kMarginTop, width() - kMarginLeft - kMarginRight,
                height() - kMarginTop - kMarginBottom);
    if (full.width() <= 0 || full.height() <= 0 || !field_ || !bbox_.valid()) return full;

    double aspect = 0.0;
    if (const auto* pg = std::get_if<core::ProjectedGrid>(&field_->grid)) {
        // A projected grid is drawn in index space, where the true aspect is the
        // physical extent in metres — not the lat/lon bbox, which is an envelope
        // that bulges well past the grid and would stretch the plot.
        const double wm = std::abs(pg->dx) * (pg->nx - 1);
        const double hm = std::abs(pg->dy) * (pg->ny - 1);
        if (wm <= 0 || hm <= 0) return full;
        aspect = wm / hm;
    } else {
        // Preserve geographic aspect (weighted by cos of mean latitude).
        const double lonSpan = bbox_.maxLon - bbox_.minLon;
        const double latSpan = bbox_.maxLat - bbox_.minLat;
        if (lonSpan <= 0 || latSpan <= 0) return full;
        const double meanLat = 0.5 * (bbox_.minLat + bbox_.maxLat);
        aspect = (lonSpan * std::cos(meanLat * std::numbers::pi / 180.0)) / latSpan;
    }
    if (!(aspect > 0.0)) return full;

    double w = full.width();
    double h = w / aspect;
    if (h > full.height()) {
        h = full.height();
        w = h * aspect;
    }
    const double x = full.left() + 0.5 * (full.width() - w);
    const double y = full.top() + 0.5 * (full.height() - h);
    return {x, y, w, h};
}

// Trace parallels and meridians through index space. A projected grid's rows and
// columns are not lines of constant latitude/longitude, so each one is sampled
// point-by-point via latlonToIndex and drawn as a polyline; that is also what
// makes the labels honest about where a degree actually falls in the raster.
void PlotView2D::drawGraticule(QPainter& p, const QRectF& r) const {
    if (!field_ || !bbox_.valid()) return;
    const double latStep = niceStep(bbox_.maxLat - bbox_.minLat, 5);
    const double lonStep = niceStep(bbox_.maxLon - bbox_.minLon, 5);
    if (!(latStep > 0.0) || !(lonStep > 0.0)) return;

    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(60, 60, 60, 90));
    pen.setWidthF(0.7);
    pen.setStyle(Qt::DashLine);
    p.setPen(pen);

    constexpr int kSamples = 48;
    // `along` walks the other coordinate; `at` is the constant one.
    auto trace = [&](bool parallel, double at) {
        QPolygonF poly;
        const double lo = parallel ? bbox_.minLon : bbox_.minLat;
        const double hi = parallel ? bbox_.maxLon : bbox_.maxLat;
        for (int k = 0; k <= kSamples; ++k) {
            const double along = lo + (hi - lo) * k / kSamples;
            const core::LatLon ll = parallel ? core::LatLon{at, along} : core::LatLon{along, at};
            const core::GridIndex gi = core::latlonToIndex(field_->grid, ll);
            if (!gi.inDomain) {  // left the grid: break the line rather than bridge it
                if (poly.size() > 1) p.drawPolyline(poly);
                poly.clear();
                continue;
            }
            poly << indexToScreen(gi.x, gi.y, r);
        }
        if (poly.size() > 1) {
            p.drawPolyline(poly);
            const QPointF label = poly.first();
            p.drawText(label + QPointF(3, -3), QString::number(at, 'g', 4) + QStringLiteral("°"));
        }
    };

    for (double lat = std::ceil(bbox_.minLat / latStep) * latStep; lat <= bbox_.maxLat;
         lat += latStep)
        trace(/*parallel=*/true, lat);
    for (double lon = std::ceil(bbox_.minLon / lonStep) * lonStep; lon <= bbox_.maxLon;
         lon += lonStep)
        trace(/*parallel=*/false, lon);
    p.restore();
}

void PlotView2D::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());

    if (!field_ || image_.isNull()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a GRIB or NetCDF file to view a field"));
        return;
    }

    const QRectF r = plotRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(r, image_);
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Contour overlay.
    if (contoursEnabled_) {
        double interval = contourInterval_;
        if (!(interval > 0.0)) interval = render::niceContourInterval(cmap_.min(), cmap_.max(), 10);
        if (interval > 0.0) {
            p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor(20, 20, 20, 180));
            pen.setWidthF(0.8);
            p.setPen(pen);
            for (const auto& lvl : contours_.levels(*field_, interval)) {
                for (const auto& s : lvl.segments) {
                    p.drawLine(indexToScreen(s.x0, s.y0, r), indexToScreen(s.x1, s.y1, r));
                }
            }
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }

    // Wind barbs (mode 1), sampled on a screen lattice. The lattice point is
    // resolved to a grid index the same way the raster underneath it was drawn, so
    // a barb sits on the cell it describes on every grid type.
    if (windMode_ == 1 && wind_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(20, 20, 20, 220), 1.0));
        p.setBrush(QColor(20, 20, 20, 220));
        const int spacing = 44;
        for (double sy = r.top() + spacing / 2.0; sy < r.bottom(); sy += spacing) {
            for (double sx = r.left() + spacing / 2.0; sx < r.right(); sx += spacing) {
                double bcol = 0.0, brow = 0.0;
                if (!screenToIndex(QPointF(sx, sy), r, bcol, brow)) continue;
                const analysis::UV uv = analysis::sampleWind(*wind_, bcol, brow);
                if (std::isnan(uv.u) || std::isnan(uv.v)) continue;
                const double speed = std::hypot(uv.u, uv.v);
                const render::WindBarb barb = render::makeWindBarb({sx, sy}, QPointF(-uv.u, uv.v),
                                                                   analysis::toKnots(speed), 20.0);
                if (barb.calm) continue;
                for (const QLineF& l : barb.lines) p.drawLine(l);
                for (const QPolygonF& tri : barb.pennants) p.drawPolygon(tri);
            }
        }
        p.setBrush(Qt::NoBrush);
    }

    // Axis ticks (slightly smaller font; guard against pixel-sized fonts whose
    // pointSizeF() is -1).
    QFont f = p.font();
    if (f.pointSizeF() > 2.0) f.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(f);

    // Axis labels. On a regular lat/lon grid, index space is linear in lat/lon, so
    // evenly-spaced degree ticks land where they claim to. On a projected grid it
    // is not — a straight tick at a fixed screen fraction would be off by degrees
    // — so the axes are labelled with grid indices and geography is carried by the
    // graticule below, which is traced through the projection.
    if (isLatLonGrid()) {
        const double lonStep = niceStep(bbox_.maxLon - bbox_.minLon, 6);
        const double latStep = niceStep(bbox_.maxLat - bbox_.minLat, 6);

        const double lonStart = std::ceil(bbox_.minLon / lonStep) * lonStep;
        for (double lon = lonStart; lon <= bbox_.maxLon + 1e-6; lon += lonStep) {
            const double fx = (lon - bbox_.minLon) / (bbox_.maxLon - bbox_.minLon);
            const double x = r.left() + fx * r.width();
            p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
            p.drawText(QRectF(x - 30, r.bottom() + 5, 60, 16), Qt::AlignHCenter | Qt::AlignTop,
                       QString::number(lon, 'g', 4) + QStringLiteral("°"));
        }

        const double latStart = std::ceil(bbox_.minLat / latStep) * latStep;
        for (double lat = latStart; lat <= bbox_.maxLat + 1e-6; lat += latStep) {
            const double fy = (bbox_.maxLat - lat) / (bbox_.maxLat - bbox_.minLat);
            const double y = r.top() + fy * r.height();
            p.drawLine(QPointF(r.left() - 4, y), QPointF(r.left(), y));
            p.drawText(QRectF(0, y - 8, kMarginLeft - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(lat, 'g', 4) + QStringLiteral("°"));
        }
    } else {
        drawGraticule(p, r);

        const int nx = core::gridWidth(field_->grid);
        const int ny = core::gridHeight(field_->grid);
        const double iStep = niceStep(nx, 6);
        const double jStep = niceStep(ny, 6);
        for (double i = 0; i <= nx - 1; i += iStep) {
            const double x = indexToScreen(i, 0, r).x();
            p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
            p.drawText(QRectF(x - 30, r.bottom() + 5, 60, 16), Qt::AlignHCenter | Qt::AlignTop,
                       QStringLiteral("i %1").arg(static_cast<int>(i)));
        }
        for (double j = 0; j <= ny - 1; j += jStep) {
            const double y = indexToScreen(0, j, r).y();
            p.drawLine(QPointF(r.left() - 4, y), QPointF(r.left(), y));
            p.drawText(QRectF(0, y - 8, kMarginLeft - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QStringLiteral("j %1").arg(static_cast<int>(j)));
        }
    }

    if (hoverActive_) paintHoverReadout(p, r, hoverPos_, hoverLines_, palette());
}

void PlotView2D::mouseMoveEvent(QMouseEvent* event) {
    const bool wasActive = hoverActive_;
    hoverActive_ = false;
    if (!field_) {
        emit probeLeft();
        if (wasActive) update();
        return;
    }
    const QRectF r = plotRect();
    const QPointF pos = event->position();
    double col = 0.0, row = 0.0;
    if (!r.contains(pos) || !screenToIndex(pos, r, col, row)) {
        emit probeLeft();
        if (wasActive) update();
        return;
    }

    // Go screen -> grid index -> lat/lon, never screen -> lat/lon directly: the
    // raster is drawn in index space, and only on a regular lat/lon grid is that
    // linear in lat/lon. Sampling by index also samples exactly the cell drawn
    // under the cursor.
    const core::LatLon ll = core::indexToLatLon(field_->grid, col, row);
    const float v = analysis::sampleBilinearIndex(*field_, col, row);
    emit probeMoved(ll.lat, ll.lon, static_cast<double>(v), !std::isnan(v));

    // In-view badge (the status bar carries the same numbers, but a floating view
    // can be far from it). Repainting is cheap: the raster and the isolines are cached.
    if (!HoverOptions::instance().enabled(HoverView::Plot)) {
        if (wasActive) update();
        return;
    }
    hoverActive_ = true;
    hoverPos_ = pos;
    hoverLines_ = hoverTextAt(ll, v, col, row);
    update();
}

// Badge lines shared with MapView's readout: position, value, and the grid cell the
// sample came from (which tells you the data's real resolution under the cursor).
// The cell comes in as the index that was already resolved from the cursor, rather
// than being re-derived from lat/lon — on a projected grid the round trip through
// PROJ is both wasted work and a source of disagreement with the sampled value.
QStringList PlotView2D::hoverTextAt(core::LatLon ll, float value, double col, double row) const {
    const int prec = field_ ? coordPrecision(core::gridSpacingDeg(field_->grid)) : 2;
    QStringList lines;
    if (std::isnan(ll.lat) || std::isnan(ll.lon)) lines << tr("(position unavailable)");
    else
        lines << QStringLiteral("lat %1°  lon %2°")
                     .arg(ll.lat, 0, 'f', prec)
                     .arg(core::wrapLon180(ll.lon), 0, 'f', prec);
    lines << (std::isnan(value) ? tr("(no data)")
                                : formatValueWithUnits(static_cast<double>(value), units()));
    lines << QStringLiteral("i %1  j %2")
                 .arg(static_cast<int>(std::lround(col)))
                 .arg(static_cast<int>(std::lround(row)));
    return lines;
}

void PlotView2D::leaveEvent(QEvent* /*event*/) {
    emit probeLeft();
    if (hoverActive_) {
        hoverActive_ = false;
        update();
    }
}

}  // namespace met::app
