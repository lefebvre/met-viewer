#include "viewer/app/crosssectionview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>

namespace met::app {
namespace {
constexpr int kML = 56, kMR = 16, kMT = 12, kMB = 34;

// Value at integer column `s` and pressure `press` (hPa), interpolating in log-p
// along that column's own (terrain-following) pressure profile. NaN if no
// bracketing level in that column is finite.
float valueAtColumn(const analysis::CrossSection& cs, int s, double press) {
    const int nl = static_cast<int>(cs.pressures.size());
    const double logP = std::log(press);
    for (int l = 0; l + 1 < nl; ++l) {
        const double pa = cs.pressures[static_cast<std::size_t>(l)][static_cast<std::size_t>(s)];
        const double pb = cs.pressures[static_cast<std::size_t>(l + 1)][static_cast<std::size_t>(s)];
        if (std::isnan(pa) || std::isnan(pb) || pa <= 0.0 || pb <= 0.0) continue;
        const double lo = std::min(pa, pb), hi = std::max(pa, pb);
        if (press >= lo && press <= hi) {
            const double denom = std::log(pb) - std::log(pa);
            const double f = denom != 0.0 ? (logP - std::log(pa)) / denom : 0.0;
            const float va = cs.values[static_cast<std::size_t>(l)][static_cast<std::size_t>(s)];
            const float vb = cs.values[static_cast<std::size_t>(l + 1)][static_cast<std::size_t>(s)];
            if (std::isnan(va) || std::isnan(vb)) return std::isnan(va) ? vb : va;
            return static_cast<float>(va * (1 - f) + vb * f);
        }
    }
    return std::numeric_limits<float>::quiet_NaN();  // outside this column's range
}

// Bilinear-ish sample at fractional column `sampleF` and pressure `press` (hPa).
float sampleSection(const analysis::CrossSection& cs, double sampleF, double press) {
    const int ns = static_cast<int>(cs.distancesKm.size());
    if (cs.pressures.empty() || ns == 0) return std::numeric_limits<float>::quiet_NaN();
    const int s0 = std::clamp(static_cast<int>(std::floor(sampleF)), 0, ns - 1);
    const int s1 = std::min(s0 + 1, ns - 1);
    const double fs = sampleF - s0;
    const float a = valueAtColumn(cs, s0, press), b = valueAtColumn(cs, s1, press);
    if (std::isnan(a)) return b;
    if (std::isnan(b)) return a;
    return static_cast<float>(a * (1 - fs) + b * fs);
}

// Global finite pressure extent across all columns/levels.
void pressureExtent(const analysis::CrossSection& cs, double& pTop, double& pBot) {
    pTop = std::numeric_limits<double>::infinity();
    pBot = -std::numeric_limits<double>::infinity();
    for (const auto& row : cs.pressures)
        for (double p : row)
            if (std::isfinite(p) && p > 0.0) {
                pTop = std::min(pTop, p);
                pBot = std::max(pBot, p);
            }
}
}  // namespace

CrossSectionView::CrossSectionView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 280);
}

void CrossSectionView::applyAutoRange() {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const auto& row : cs_.values)
        for (float x : row)
            if (!std::isnan(x)) { lo = std::min(lo, double(x)); hi = std::max(hi, double(x)); }
    if (!std::isfinite(lo) || lo == hi) { lo = 0; hi = 1; }
    min_ = lo;
    max_ = hi;
    cmap_.setRange(lo, hi);
}

void CrossSectionView::setSection(const analysis::CrossSection& cs) {
    cs_ = cs;
    if (autoRange_) applyAutoRange();
    emit rangeChanged(min_, max_);
    update();
}

void CrossSectionView::setColormapByName(const QString& name) {
    const double lo = cmap_.min(), hi = cmap_.max();
    cmap_ = render::Colormap::builtin(name.toStdString());
    cmap_.setRange(lo, hi);
    update();
}

void CrossSectionView::setAutoRange(bool on) {
    autoRange_ = on;
    if (!on) return;
    applyAutoRange();
    emit rangeChanged(min_, max_);
    update();
}

void CrossSectionView::setRange(double lo, double hi) {
    min_ = lo;
    max_ = hi;
    cmap_.setRange(lo, hi);
    emit rangeChanged(lo, hi);
    update();
}

void CrossSectionView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (cs_.pressures.size() < 2 || cs_.distancesKm.size() < 2) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Draw a cross-section path on the map"));
        return;
    }

    const QRectF r(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    const int ns = static_cast<int>(cs_.distancesKm.size());
    double pTop = 0, pBot = 0;
    pressureExtent(cs_, pTop, pBot);
    if (!(pTop > 0.0) || !(pBot > pTop)) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Cross-section has no valid pressure data"));
        return;
    }
    const double logTop = std::log(pTop), logBot = std::log(pBot);

    // Colored field: for each screen row map to a pressure, sample each column at
    // that pressure through its own (terrain-following) profile.
    const double totalKm = cs_.distancesKm.back();
    QImage img(static_cast<int>(r.width()), static_cast<int>(r.height()), QImage::Format_ARGB32);
    for (int py = 0; py < img.height(); ++py) {
        const double press = std::exp(logTop + (double(py) / (img.height() - 1)) * (logBot - logTop));
        auto* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
        for (int px = 0; px < img.width(); ++px) {
            const double sampleF = (double(px) / (img.width() - 1)) * (ns - 1);
            const float val = sampleSection(cs_, sampleF, press);
            const render::Rgba c = cmap_.map(val);
            scan[px] = qRgba(c.r, c.g, c.b, c.a);
        }
    }
    p.drawImage(r.topLeft(), img);
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Pressure axis (log): label "nice" standard levels that fall in range, since
    // per-column pressures no longer map to a single tick per level.
    static const double kStdLevels[] = {1000, 925, 850, 700, 500, 400, 300, 250,
                                        200,  150, 100, 70,  50,  30,  20,  10};
    for (double press : kStdLevels) {
        if (press < pTop || press > pBot) continue;
        const double f = (std::log(press) - logTop) / (logBot - logTop);
        const double y = r.top() + f * r.height();
        p.drawLine(QPointF(r.left() - 4, y), QPointF(r.left(), y));
        p.drawText(QRectF(0, y - 8, kML - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(press, 'g', 4));
    }
    // Distance axis.
    for (int k = 0; k <= 4; ++k) {
        const double x = r.left() + r.width() * k / 4.0;
        const double km = totalKm * k / 4.0;
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        p.drawText(QRectF(x - 40, r.bottom() + 5, 80, 16), Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(km, 'g', 4) + " km");
    }
    p.drawText(QRectF(0, 0, width(), kMT), Qt::AlignCenter,
               tr("Cross-section (%1)").arg(QString::fromStdString(cs_.units)));
}

}  // namespace met::app
