#include "viewer/app/timeseriesview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QMouseEvent>
#include <QPainter>

#include "viewer/app/hoverreadout.h"
#include "viewer/core/timeaxis.h"

namespace met::app {
namespace {
constexpr int kML = 64, kMR = 16, kMT = 22, kMB = 40;
}

TimeSeriesView::TimeSeriesView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 220);
    setMouseTracking(true);
    connect(&HoverOptions::instance(), &HoverOptions::changed, this, [this](HoverView v) {
        if (v != HoverView::TimeSeries) return;
        hoverActive_ = false;  // drop a badge left over from before the toggle
        update();
    });
}

TimeSeriesView::Layout TimeSeriesView::layout() const {
    Layout lay;
    lay.rect = QRectF(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    if (ts_.values.size() < 2 || lay.rect.width() < 2 || lay.rect.height() < 2) return lay;

    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (float v : ts_.values)
        if (!std::isnan(v)) { lo = std::min(lo, double(v)); hi = std::max(hi, double(v)); }
    if (!std::isfinite(lo)) { lo = 0; hi = 1; }
    if (lo == hi) { lo -= 1; hi += 1; }
    const double pad = 0.08 * (hi - lo);
    lay.lo = lo - pad;
    lay.hi = hi + pad;
    lay.valid = true;
    return lay;
}

void TimeSeriesView::setSeries(const analysis::TimeSeries& ts, const QString& varName) {
    ts_ = ts;
    varName_ = varName;
    update();
}

void TimeSeriesView::setCurrentIndex(int index) {
    if (index == currentIdx_) return;
    currentIdx_ = index;
    update();
}

void TimeSeriesView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (ts_.values.size() < 2) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Pick a time-series point on the map"));
        return;
    }

    const Layout lay = layout();
    if (!lay.valid) return;
    const QRectF& r = lay.rect;
    const double lo = lay.lo, hi = lay.hi;

    const int n = static_cast<int>(ts_.values.size());
    auto xOf = [&](int i) { return lay.xOf(i, n); };
    auto yOf = [&](double v) { return lay.yOf(v); };

    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Y ticks.
    for (int k = 0; k <= 4; ++k) {
        const double v = lo + (hi - lo) * k / 4.0;
        const double y = yOf(v);
        p.drawLine(QPointF(r.left() - 4, y), QPointF(r.left(), y));
        p.drawText(QRectF(0, y - 8, kML - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'g', 4));
    }
    // X ticks (time labels).
    const int stride = std::max(1, n / 6);
    for (int i = 0; i < n; i += stride) {
        const double x = xOf(i);
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        const QString label = QString::fromStdString(met::core::formatTime(ts_.times[static_cast<std::size_t>(i)]));
        p.save();
        p.translate(x, r.bottom() + 6);
        p.rotate(30);
        p.drawText(0, 0, label);
        p.restore();
    }

    // Series line + markers.
    p.setRenderHint(QPainter::Antialiasing, true);
    QPolygonF poly;
    for (int i = 0; i < n; ++i)
        if (!std::isnan(ts_.values[static_cast<std::size_t>(i)]))
            poly << QPointF(xOf(i), yOf(ts_.values[static_cast<std::size_t>(i)]));
    p.setPen(QPen(QColor(40, 90, 190), 2.0));
    p.drawPolyline(poly);
    p.setBrush(QColor(40, 90, 190));
    for (const QPointF& pt : poly) p.drawEllipse(pt, 2.5, 2.5);

    // Current-time marker (follows the time slider).
    if (currentIdx_ >= 0 && currentIdx_ < n) {
        const double x = xOf(currentIdx_);
        p.setPen(QPen(QColor(200, 60, 60, 180), 1.5));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        const float cv = ts_.values[static_cast<std::size_t>(currentIdx_)];
        if (!std::isnan(cv)) {
            p.setBrush(QColor(200, 60, 60));
            p.drawEllipse(QPointF(x, yOf(cv)), 3.5, 3.5);
        }
    }

    p.setPen(palette().color(QPalette::Text));
    p.drawText(QRectF(0, 2, width(), kMT - 2), Qt::AlignCenter,
               tr("%1 at (%2°, %3°)  [%4]")
                   .arg(varName_)
                   .arg(ts_.point.lat, 0, 'f', coordPrec_)
                   .arg(ts_.point.lon, 0, 'f', coordPrec_)
                   .arg(QString::fromStdString(ts_.units)));

    // Cursor readout, snapped to the nearest sample so the badge names a real point.
    if (hoverActive_ && hoverIdx_ >= 0 && hoverIdx_ < n) {
        const float hv = ts_.values[static_cast<std::size_t>(hoverIdx_)];
        if (!std::isnan(hv)) {
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(palette().color(QPalette::Text), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(xOf(hoverIdx_), yOf(hv)), 4.5, 4.5);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        paintHoverReadout(p, r, hoverPos_, hoverLines_, palette());
    }
}

void TimeSeriesView::mouseMoveEvent(QMouseEvent* event) {
    const bool wasActive = hoverActive_;
    hoverActive_ = false;
    const Layout lay = layout();
    const QPointF pos = event->position();
    if (!lay.valid || !lay.rect.contains(pos) ||
        !HoverOptions::instance().enabled(HoverView::TimeSeries)) {
        if (wasActive) update();
        return;
    }

    // Snap to the nearest time step: between samples there is no data to report.
    const int n = static_cast<int>(ts_.values.size());
    const double f = (pos.x() - lay.rect.left()) / lay.rect.width() * (n - 1);
    const int i = std::clamp(static_cast<int>(std::lround(f)), 0, n - 1);
    const float v = ts_.values[static_cast<std::size_t>(i)];

    QStringList lines;
    lines << QString::fromStdString(core::formatTime(ts_.times[static_cast<std::size_t>(i)]));
    lines << (std::isnan(v) ? tr("(no data)")
                            : formatValueWithUnits(static_cast<double>(v),
                                                   QString::fromStdString(ts_.units)));

    hoverActive_ = true;
    hoverIdx_ = i;
    // Anchor the crosshair on the snapped sample, not the raw pointer, so the badge
    // and the highlighted point line up.
    hoverPos_ = QPointF(lay.xOf(i, n), std::isnan(v) ? pos.y() : lay.yOf(v));
    hoverLines_ = lines;
    update();
}

void TimeSeriesView::leaveEvent(QEvent*) {
    if (!hoverActive_) return;
    hoverActive_ = false;
    update();
}

}  // namespace met::app
