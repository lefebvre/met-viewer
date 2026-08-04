#include "viewer/app/crosssectionview.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

#include "viewer/app/hoverreadout.h"
#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/core/grid.h"
#include "viewer/render/contour.h"

namespace met::app {
namespace {
constexpr int kML = 56, kMR = 16, kMT = 12, kMB = 34;
// Spacing (px) of the lattice the height field is resampled onto before marching
// squares. Fine enough that the isopleths look smooth, coarse enough that a
// rebuild is a few thousand log-p lookups rather than one per pixel.
constexpr std::size_t kContourStep = 4;
constexpr int kHeightContourTarget = 8;  // isopleths to aim for across the section

// A [level][sample] quantity of the section — the values, or the heights — at
// integer column `s` and pressure `press` (hPa), interpolating in log-p along that
// column's own (terrain-following) pressure profile. NaN if no bracketing level in
// that column is finite.
template <typename T>
double rowsAtColumn(const analysis::CrossSection& cs, const std::vector<std::vector<T>>& rows,
                    std::size_t s, double press) {
    const std::size_t nl = std::min(cs.pressures.size(), rows.size());
    const double logP = std::log(press);
    for (std::size_t l = 0; l + 1 < nl; ++l) {
        const double pa = cs.pressures[l][s];
        const double pb = cs.pressures[l + 1][s];
        if (std::isnan(pa) || std::isnan(pb) || pa <= 0.0 || pb <= 0.0) continue;
        const double lo = std::min(pa, pb), hi = std::max(pa, pb);
        if (press >= lo && press <= hi) {
            const double denom = std::log(pb) - std::log(pa);
            const double f = denom != 0.0 ? (logP - std::log(pa)) / denom : 0.0;
            const double va = rows[l][s];
            const double vb = rows[l + 1][s];
            if (std::isnan(va) || std::isnan(vb)) return std::isnan(va) ? vb : va;
            return std::lerp(va, vb, f);
        }
    }
    return std::numeric_limits<double>::quiet_NaN();  // outside this column's range
}

// Bilinear-ish sample of `rows` at fractional column `sampleF` and pressure `press`.
template <typename T>
double sampleRows(const analysis::CrossSection& cs, const std::vector<std::vector<T>>& rows,
                  double sampleF, double press) {
    const std::size_t ns = cs.distancesKm.size();
    if (cs.pressures.empty() || rows.empty() || ns == 0)
        return std::numeric_limits<double>::quiet_NaN();
    const std::size_t last = ns - 1;
    const double sf = std::floor(sampleF);
    const std::size_t s0 = sf <= 0.0 ? 0 : std::min(last, static_cast<std::size_t>(sf));
    const std::size_t s1 = std::min(s0 + 1, last);
    const double fs = sampleF - static_cast<double>(s0);
    const double a = rowsAtColumn(cs, rows, s0, press), b = rowsAtColumn(cs, rows, s1, press);
    if (std::isnan(a)) return b;
    if (std::isnan(b)) return a;
    return std::lerp(a, b, fs);
}

// The section's variable at (column, pressure).
float sampleSection(const analysis::CrossSection& cs, double sampleF, double press) {
    return static_cast<float>(sampleRows(cs, cs.values, sampleF, press));
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
            if (!std::isnan(x)) {
                lo = std::min(lo, double(x));
                hi = std::max(hi, double(x));
            }
    if (!std::isfinite(lo) || lo == hi) {
        lo = 0;
        hi = 1;
    }
    min_ = lo;
    max_ = hi;
    cmap_.setRange(lo, hi);
}

void CrossSectionView::setSection(const analysis::CrossSection& cs) {
    cs_ = cs;
    ++sectionSeq_;  // invalidates img_
    if (autoRange_) applyAutoRange();
    emit rangeChanged(min_, max_);
    emit heightsAvailableChanged(hasHeights());
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

void CrossSectionView::setHeightContoursEnabled(bool on) {
    if (showHeights_ == on) return;
    showHeights_ = on;
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
    lay.valid =
        lay.pTop > 0.0 && lay.pBot > lay.pTop && lay.rect.width() >= 2 && lay.rect.height() >= 2;
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

void CrossSectionView::rebuildHeightContours(const Layout& lay) {
    const QSize size(static_cast<int>(lay.rect.width()), static_cast<int>(lay.rect.height()));
    if (contourSection_ == sectionSeq_ && contourSize_ == size) return;
    contourSection_ = sectionSeq_;
    contourSize_ = size;
    heightContours_.clear();
    if (cs_.heights.empty() || size.width() < 8 || size.height() < 8) return;

    // Resample height onto a regular screen-space lattice. It is a 2-D field over
    // (distance, log-p) exactly like the colormapped image underneath, and
    // marching squares wants a regular grid; a Field2D is that grid, its geographic
    // metadata unused because the segments come back in index space.
    // size is at least 8x8 here, so the casts to unsigned below are well defined.
    const std::size_t nx =
        std::max<std::size_t>(2, static_cast<std::size_t>(size.width()) / kContourStep + 1);
    const std::size_t ny =
        std::max<std::size_t>(2, static_cast<std::size_t>(size.height()) / kContourStep + 1);
    core::Field2D field;
    field.grid = core::RegularLatLonGrid{
        0.0, 0.0, 1.0, 1.0, static_cast<int>(nx), static_cast<int>(ny), false};
    field.values.assign(nx * ny, std::numeric_limits<float>::quiet_NaN());

    const double logTop = std::log(lay.pTop), logBot = std::log(lay.pBot);
    const int ns = static_cast<int>(cs_.distancesKm.size());
    for (std::size_t j = 0; j < ny; ++j) {
        const double fy = static_cast<double>(j) / static_cast<double>(ny - 1);
        const double press = std::exp(logTop + fy * (logBot - logTop));
        for (std::size_t i = 0; i < nx; ++i) {
            const double sampleF = static_cast<double>(i) / static_cast<double>(nx - 1) * (ns - 1);
            field.values[j * nx + i] =
                static_cast<float>(sampleRows(cs_, cs_.heights, sampleF, press));
        }
    }

    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (float v : field.values)
        if (!std::isnan(v)) {
            lo = std::min(lo, double(v));
            hi = std::max(hi, double(v));
        }
    const double interval = render::niceContourInterval(lo, hi, kHeightContourTarget);
    if (!(interval > 0.0)) return;

    // Lattice index -> widget coordinates; the lattice spans the plot rect exactly.
    const double sx = lay.rect.width() / static_cast<double>(nx - 1),
                 sy = lay.rect.height() / static_cast<double>(ny - 1);
    auto toWidget = [&](double x, double y) {
        return QPointF(lay.rect.left() + x * sx, lay.rect.top() + y * sy);
    };
    for (const auto& lvl : render::contourLevels(field, interval)) {
        HeightContour hc;
        hc.gpm = lvl.value;
        hc.lines.reserve(lvl.segments.size());
        for (const auto& s : lvl.segments)
            hc.lines.emplace_back(toWidget(s.x0, s.y0), toWidget(s.x1, s.y1));
        if (!hc.lines.empty()) heightContours_.push_back(std::move(hc));
    }
}

// Draw the height isopleths and label them. Each line is stroked twice — a pale
// halo under a dark line — because a single colour cannot stay legible across a
// whole colormap.
void CrossSectionView::paintHeightContours(QPainter& p, const QRectF& r) const {
    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (int pass = 0; pass < 2; ++pass) {
        QPen pen(pass == 0 ? QColor(255, 255, 255, 150) : QColor(25, 25, 25, 200));
        pen.setWidthF(pass == 0 ? 2.6 : 1.0);
        p.setPen(pen);
        for (const auto& hc : heightContours_)
            for (const QLineF& l : hc.lines) p.drawLine(l);
    }

    // One label per isopleth, on the segment nearest a fixed column so the labels
    // line up rather than scattering along the lines.
    const QFontMetrics fm(p.font());
    const double labelX = r.left() + r.width() * 0.22;
    for (const auto& hc : heightContours_) {
        const QLineF* best = nullptr;
        double bestDx = std::numeric_limits<double>::infinity();
        for (const QLineF& l : hc.lines) {
            const double dx = std::abs(l.center().x() - labelX);
            if (dx < bestDx) {
                bestDx = dx;
                best = &l;
            }
        }
        if (!best) continue;
        const QString text = formatHeight(hc.gpm);
        const QRectF box(best->center().x() - fm.horizontalAdvance(text) / 2.0 - 3,
                         best->center().y() - fm.height() / 2.0 - 1, fm.horizontalAdvance(text) + 6,
                         fm.height() + 2);
        if (!r.contains(box)) continue;  // a half-clipped label reads as a wrong number
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 190));
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        p.setPen(QColor(25, 25, 25));
        p.drawText(box, Qt::AlignCenter, text);
    }
    p.restore();
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
    rebuildHeightContours(lay);
    if (showHeights_) paintHeightContours(p, r);
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
    const std::size_t s0 =
        static_cast<std::size_t>(std::clamp(std::floor(sampleF), 0.0, static_cast<double>(ns - 1)));
    const std::size_t s1 = std::min(s0 + 1, static_cast<std::size_t>(ns - 1));
    const double f = sampleF - static_cast<double>(s0);
    const double km = std::lerp(cs_.distancesKm[s0], cs_.distancesKm[s1], f);

    QStringList lines;
    if (s0 < cs_.points.size() && s1 < cs_.points.size()) {
        const core::LatLon a = cs_.points[s0], b = cs_.points[s1];
        lines << QStringLiteral("%1 km  (%2°, %3°)")
                     .arg(km, 0, 'f', 1)
                     .arg(std::lerp(a.lat, b.lat, f), 0, 'f', coordPrec_)
                     .arg(core::wrapLon180(std::lerp(a.lon, b.lon, f)), 0, 'f', coordPrec_);
    } else {
        lines << QStringLiteral("%1 km").arg(km, 0, 'f', 1);
    }
    QString pressLine = QStringLiteral("%1 hPa").arg(press, 0, 'f', 1);
    if (!cs_.heights.empty()) {
        const double z = sampleRows(cs_, cs_.heights, sampleF, press);
        if (!std::isnan(z)) pressLine += QStringLiteral("   Z %1 m").arg(z, 0, 'f', 0);
    }
    lines << pressLine;
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
