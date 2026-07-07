#include "viewer/app/timeseriesview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>

#include "viewer/core/timeaxis.h"

namespace met::app {
namespace {
constexpr int kML = 64, kMR = 16, kMT = 22, kMB = 40;
}

TimeSeriesView::TimeSeriesView(QWidget* parent) : QWidget(parent) { setMinimumSize(360, 220); }

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

    const QRectF r(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (float v : ts_.values)
        if (!std::isnan(v)) { lo = std::min(lo, double(v)); hi = std::max(hi, double(v)); }
    if (!std::isfinite(lo)) { lo = 0; hi = 1; }
    if (lo == hi) { lo -= 1; hi += 1; }
    const double pad = 0.08 * (hi - lo);
    lo -= pad; hi += pad;

    const int n = static_cast<int>(ts_.values.size());
    auto xOf = [&](int i) { return r.left() + r.width() * i / (n - 1); };
    auto yOf = [&](double v) { return r.bottom() - (v - lo) / (hi - lo) * r.height(); };

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
                   .arg(ts_.point.lat, 0, 'f', 1)
                   .arg(ts_.point.lon, 0, 'f', 1)
                   .arg(QString::fromStdString(ts_.units)));
}

}  // namespace met::app
