#include "viewer/app/plotview2d.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

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

QPointF PlotView2D::indexToScreen(double col, double row, const QRectF& r) const {
    // Flat index-space mapping matching render::fieldToImage, so contour lines
    // (expressed in grid-index coordinates) stay aligned with the raster rather
    // than being warped through the map projection. For a regular lat/lon grid
    // this is algebraically identical to the old lat/lon routing.
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
    if (full.width() <= 0 || full.height() <= 0 || !bbox_.valid()) return full;

    // Preserve geographic aspect (weighted by cos of mean latitude).
    const double lonSpan = bbox_.maxLon - bbox_.minLon;
    const double latSpan = bbox_.maxLat - bbox_.minLat;
    if (lonSpan <= 0 || latSpan <= 0) return full;
    const double meanLat = 0.5 * (bbox_.minLat + bbox_.maxLat);
    const double aspect = (lonSpan * std::cos(meanLat * std::numbers::pi / 180.0)) / latSpan;

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
        if (!(interval > 0.0))
            interval = render::niceContourInterval(cmap_.min(), cmap_.max(), 10);
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

    // Wind barbs (mode 1), sampled on a screen lattice via the geographic map.
    if (windMode_ == 1 && wind_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(20, 20, 20, 220), 1.0));
        p.setBrush(QColor(20, 20, 20, 220));
        const int spacing = 44;
        const double lonSpan = bbox_.maxLon - bbox_.minLon;
        const double latSpan = bbox_.maxLat - bbox_.minLat;
        for (double sy = r.top() + spacing / 2.0; sy < r.bottom(); sy += spacing) {
            for (double sx = r.left() + spacing / 2.0; sx < r.right(); sx += spacing) {
                const double lon = bbox_.minLon + (sx - r.left()) / r.width() * lonSpan;
                const double lat = bbox_.maxLat - (sy - r.top()) / r.height() * latSpan;
                const analysis::UV uv = analysis::sampleWindLatLon(*wind_, core::LatLon{lat, lon});
                if (std::isnan(uv.u) || std::isnan(uv.v)) continue;
                const double speed = std::hypot(uv.u, uv.v);
                const render::WindBarb barb = render::makeWindBarb(
                    {sx, sy}, QPointF(-uv.u, uv.v), analysis::toKnots(speed), 20.0);
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
    if (f.pointSizeF() > 2.0)
        f.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(f);

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
    if (!r.contains(pos)) {
        emit probeLeft();
        if (wasActive) update();
        return;
    }

    const double fx = (pos.x() - r.left()) / r.width();
    const double fy = (pos.y() - r.top()) / r.height();
    const double lon = bbox_.minLon + fx * (bbox_.maxLon - bbox_.minLon);
    const double lat = bbox_.maxLat - fy * (bbox_.maxLat - bbox_.minLat);

    const core::LatLon ll{lat, lon};
    const float v = analysis::sampleBilinear(*field_, ll);
    emit probeMoved(lat, lon, static_cast<double>(v), !std::isnan(v));

    // In-view badge (the status bar carries the same numbers, but a floating view
    // can be far from it). Repainting is cheap: the raster and the isolines are cached.
    if (!HoverOptions::instance().enabled(HoverView::Plot)) {
        if (wasActive) update();
        return;
    }
    hoverActive_ = true;
    hoverPos_ = pos;
    hoverLines_ = hoverTextAt(ll, v);
    update();
}

// Badge lines shared with MapView's readout: position, value, and the grid cell the
// sample came from (which tells you the data's real resolution under the cursor).
QStringList PlotView2D::hoverTextAt(core::LatLon ll, float value) const {
    QStringList lines;
    lines << QStringLiteral("lat %1°  lon %2°")
                 .arg(ll.lat, 0, 'f', 2)
                 .arg(core::wrapLon180(ll.lon), 0, 'f', 2);
    lines << (std::isnan(value) ? tr("(no data)")
                                : formatValueWithUnits(static_cast<double>(value), units()));
    if (field_) {
        const core::GridIndex gi = core::latlonToIndex(field_->grid, ll);
        if (gi.inDomain)
            lines << QStringLiteral("i %1  j %2")
                         .arg(static_cast<int>(std::lround(gi.x)))
                         .arg(static_cast<int>(std::lround(gi.y)));
    }
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
