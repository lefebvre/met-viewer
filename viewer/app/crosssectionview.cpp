#include "viewer/app/crosssectionview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>

namespace met::app {
namespace {
constexpr int kML = 56, kMR = 16, kMT = 12, kMB = 34;

// Interpolate a value column at fractional sample index and level (in log-p).
float sampleSection(const analysis::CrossSection& cs, double sampleF, double levelF) {
    const int nl = static_cast<int>(cs.pressures.size());
    const int ns = static_cast<int>(cs.distancesKm.size());
    if (nl == 0 || ns == 0) return std::numeric_limits<float>::quiet_NaN();
    const int s0 = std::clamp(static_cast<int>(std::floor(sampleF)), 0, ns - 1);
    const int s1 = std::min(s0 + 1, ns - 1);
    const int l0 = std::clamp(static_cast<int>(std::floor(levelF)), 0, nl - 1);
    const int l1 = std::min(l0 + 1, nl - 1);
    const double fs = sampleF - s0, fl = levelF - l0;
    auto v = [&](int l, int s) { return cs.values[static_cast<std::size_t>(l)][static_cast<std::size_t>(s)]; };
    const float a = v(l0, s0), b = v(l0, s1), c = v(l1, s0), d = v(l1, s1);
    if (std::isnan(a) || std::isnan(b) || std::isnan(c) || std::isnan(d))
        return v(l0, s0);
    return static_cast<float>((a * (1 - fs) + b * fs) * (1 - fl) + (c * (1 - fs) + d * fs) * fl);
}
}  // namespace

CrossSectionView::CrossSectionView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 280);
}

void CrossSectionView::setSection(const analysis::CrossSection& cs) {
    cs_ = cs;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (const auto& row : cs_.values)
        for (float x : row)
            if (!std::isnan(x)) { lo = std::min(lo, double(x)); hi = std::max(hi, double(x)); }
    if (!std::isfinite(lo) || lo == hi) { lo = 0; hi = 1; }
    min_ = lo; max_ = hi;
    cmap_.setRange(lo, hi);
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
    const int nl = static_cast<int>(cs_.pressures.size());
    const double pTop = *std::min_element(cs_.pressures.begin(), cs_.pressures.end());
    const double pBot = *std::max_element(cs_.pressures.begin(), cs_.pressures.end());
    const double logTop = std::log(pTop), logBot = std::log(pBot);

    // Map a pressure to a fractional level index by searching the (sorted top->
    // bottom) pressures array in log space.
    auto levelIndexForPressure = [&](double press) {
        for (int l = 0; l + 1 < nl; ++l) {
            const double a = cs_.pressures[static_cast<std::size_t>(l)];
            const double b = cs_.pressures[static_cast<std::size_t>(l + 1)];
            const double lo = std::min(a, b), hi = std::max(a, b);
            if (press >= lo && press <= hi) {
                const double f = (std::log(press) - std::log(a)) / (std::log(b) - std::log(a));
                return l + f;
            }
        }
        return press <= pTop ? 0.0 : double(nl - 1);
    };

    // Colored field.
    const double totalKm = cs_.distancesKm.back();
    QImage img(static_cast<int>(r.width()), static_cast<int>(r.height()), QImage::Format_ARGB32);
    for (int py = 0; py < img.height(); ++py) {
        const double press = std::exp(logTop + (double(py) / (img.height() - 1)) * (logBot - logTop));
        const double levelF = levelIndexForPressure(press);
        auto* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
        for (int px = 0; px < img.width(); ++px) {
            const double sampleF = (double(px) / (img.width() - 1)) * (ns - 1);
            const float val = sampleSection(cs_, sampleF, levelF);
            const render::Rgba c = cmap_.map(val);
            scan[px] = qRgba(c.r, c.g, c.b, c.a);
        }
    }
    p.drawImage(r.topLeft(), img);
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Pressure axis (log).
    for (double press : cs_.pressures) {
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
