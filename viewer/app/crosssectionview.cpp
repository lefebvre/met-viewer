#include "viewer/app/crosssectionview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QMouseEvent>
#include <QPainter>

#include "viewer/app/hoverreadout.h"
#include "viewer/core/geo.h"

namespace met::app {
namespace {
constexpr int kML = 56, kMR = 16, kMT = 12, kMB = 34;

// Value at integer column `s` and pressure `press` (hPa), interpolating in log-p
// along that column's own (terrain-following) pressure profile. NaN if no
// bracketing level in that column is finite.
float valueAtColumn(const analysis::CrossSection& cs, std::size_t s, double press) {
    const std::size_t nl = cs.pressures.size();
    const double logP = std::log(press);
    for (std::size_t l = 0; l + 1 < nl; ++l) {
        const double pa = cs.pressures[l][s];
        const double pb = cs.pressures[l + 1][s];
        if (std::isnan(pa) || std::isnan(pb) || pa <= 0.0 || pb <= 0.0) continue;
        const double lo = std::min(pa, pb), hi = std::max(pa, pb);
        if (press >= lo && press <= hi) {
            const double denom = std::log(pb) - std::log(pa);
            const double f = denom != 0.0 ? (logP - std::log(pa)) / denom : 0.0;
            const float va = cs.values[l][s];
            const float vb = cs.values[l + 1][s];
            if (std::isnan(va) || std::isnan(vb)) return std::isnan(va) ? vb : va;
            return static_cast<float>(std::lerp(va, vb, f));
        }
    }
    return std::numeric_limits<float>::quiet_NaN();  // outside this column's range
}

// Bilinear-ish sample at fractional column `sampleF` and pressure `press` (hPa).
float sampleSection(const analysis::CrossSection& cs, double sampleF, double press) {
    const std::size_t ns = cs.distancesKm.size();
    if (cs.pressures.empty() || ns == 0) return std::numeric_limits<float>::quiet_NaN();
    const std::size_t last = ns - 1;
    const double sf = std::floor(sampleF);
    const std::size_t s0 = sf <= 0.0 ? 0 : std::min(last, static_cast<std::size_t>(sf));
    const std::size_t s1 = std::min(s0 + 1, last);
    const double fs = sampleF - static_cast<double>(s0);
    const float a = valueAtColumn(cs, s0, press), b = valueAtColumn(cs, s1, press);
    if (std::isnan(a)) return b;
    if (std::isnan(b)) return a;
    return static_cast<float>(std::lerp(a, b, fs));
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
    setMouseTracking(true);
    connect(&HoverOptions::instance(), &HoverOptions::changed, this, [this](HoverView v) {
        if (v != HoverView::CrossSection) return;
        hoverActive_ = false;  // drop a badge left over from before the toggle
        update();
    });
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
    ++sectionSeq_;  // invalidates img_
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

CrossSectionView::Layout CrossSectionView::layout() const {
    Layout lay;
    lay.rect = QRectF(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    if (cs_.pressures.size() < 2 || cs_.distancesKm.size() < 2) return lay;
    pressureExtent(cs_, lay.pTop, lay.pBot);
    lay.valid = lay.pTop > 0.0 && lay.pBot > lay.pTop && lay.rect.width() >= 2 &&
                lay.rect.height() >= 2;
    return lay;
}

void CrossSectionView::rebuildImage(const Layout& lay) {
    const QSize size(static_cast<int>(lay.rect.width()), static_cast<int>(lay.rect.height()));
    if (!img_.isNull() && imgSize_ == size && imgSection_ == sectionSeq_ &&
        imgCmap_ == QString::fromStdString(cmap_.name()) && imgMin_ == cmap_.min() &&
        imgMax_ == cmap_.max())
        return;

    // For each screen row map to a pressure, then sample each column at that
    // pressure through its own (terrain-following) profile.
    const int ns = static_cast<int>(cs_.distancesKm.size());
    const double logTop = std::log(lay.pTop), logBot = std::log(lay.pBot);
    img_ = QImage(size, QImage::Format_ARGB32);
    for (int py = 0; py < img_.height(); ++py) {
        const double press =
            std::exp(logTop + (double(py) / (img_.height() - 1)) * (logBot - logTop));
        auto* scan = reinterpret_cast<QRgb*>(img_.scanLine(py));
        for (int px = 0; px < img_.width(); ++px) {
            const double sampleF = (double(px) / (img_.width() - 1)) * (ns - 1);
            const float val = sampleSection(cs_, sampleF, press);
            const render::Rgba c = cmap_.map(val);
            scan[px] = qRgba(c.r, c.g, c.b, c.a);
        }
    }
    imgSize_ = size;
    imgSection_ = sectionSeq_;
    imgCmap_ = QString::fromStdString(cmap_.name());
    imgMin_ = cmap_.min();
    imgMax_ = cmap_.max();
}

void CrossSectionView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (cs_.pressures.size() < 2 || cs_.distancesKm.size() < 2) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Draw a cross-section path on the map"));
        return;
    }

    const Layout lay = layout();
    const QRectF r = lay.rect;
    if (!lay.valid) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Cross-section has no valid pressure data"));
        return;
    }
    const double pTop = lay.pTop, pBot = lay.pBot;
    const double logTop = std::log(pTop), logBot = std::log(pBot);
    const double totalKm = cs_.distancesKm.back();

    rebuildImage(lay);
    p.drawImage(r.topLeft(), img_);
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

    if (hoverActive_) paintHoverReadout(p, r, hoverPos_, hoverLines_, palette());
}

void CrossSectionView::mouseMoveEvent(QMouseEvent* event) {
    const bool wasActive = hoverActive_;
    hoverActive_ = false;
    const Layout lay = layout();
    const QPointF pos = event->position();
    if (!lay.valid || !lay.rect.contains(pos) ||
        !HoverOptions::instance().enabled(HoverView::CrossSection)) {
        if (wasActive) update();
        return;
    }

    // Invert the paint mapping: x -> fractional column along the path, y -> pressure
    // on the log axis. Both must match layout()/rebuildImage() exactly.
    const QRectF& r = lay.rect;
    const int ns = static_cast<int>(cs_.distancesKm.size());
    const double sampleF = (pos.x() - r.left()) / r.width() * (ns - 1);
    const double logTop = std::log(lay.pTop), logBot = std::log(lay.pBot);
    const double press = std::exp(logTop + (pos.y() - r.top()) / r.height() * (logBot - logTop));

    // Distance and position at this column, interpolated between path samples.
    const std::size_t s0 = static_cast<std::size_t>(
        std::clamp(std::floor(sampleF), 0.0, static_cast<double>(ns - 1)));
    const std::size_t s1 = std::min(s0 + 1, static_cast<std::size_t>(ns - 1));
    const double f = sampleF - static_cast<double>(s0);
    const double km = std::lerp(cs_.distancesKm[s0], cs_.distancesKm[s1], f);

    QStringList lines;
    if (s0 < cs_.points.size() && s1 < cs_.points.size()) {
        const core::LatLon a = cs_.points[s0], b = cs_.points[s1];
        lines << QStringLiteral("%1 km  (%2°, %3°)")
                     .arg(km, 0, 'f', 1)
                     .arg(std::lerp(a.lat, b.lat, f), 0, 'f', 2)
                     .arg(core::wrapLon180(std::lerp(a.lon, b.lon, f)), 0, 'f', 2);
    } else {
        lines << QStringLiteral("%1 km").arg(km, 0, 'f', 1);
    }
    lines << QStringLiteral("%1 hPa").arg(press, 0, 'f', 1);
    const float v = sampleSection(cs_, sampleF, press);
    lines << (std::isnan(v) ? tr("(no data)")
                            : formatValueWithUnits(static_cast<double>(v),
                                                   QString::fromStdString(cs_.units)));

    hoverActive_ = true;
    hoverPos_ = pos;
    hoverLines_ = lines;
    update();
}

void CrossSectionView::leaveEvent(QEvent*) {
    if (!hoverActive_) return;
    hoverActive_ = false;
    update();
}

}  // namespace met::app
