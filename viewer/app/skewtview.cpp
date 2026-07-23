#include "viewer/app/skewtview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "viewer/analysis/wind.h"
#include "viewer/app/hoverreadout.h"
#include "viewer/render/windbarb.h"

namespace met::app {
namespace {
constexpr int kML = 44, kMR = 58, kMT = 24, kMB = 30;  // wide right margin: wind column
constexpr double kPtop = 100.0, kPbot = 1050.0;   // pressure axis (hPa)
constexpr double kTmin = -40.0, kTmax = 40.0;     // temperature at the bottom (°C)
constexpr double kSkew = 0.85;                    // px of x per px of height

// Inverse Magnus: dewpoint/temperature (°C) whose saturation vapour pressure is es (hPa).
double tempForEs(double es) {
    const double l = std::log(es / 6.112);
    return 243.12 * l / (17.62 - l);
}

// The sounding interpolated to an arbitrary pressure, linearly in log-p between the
// bracketing levels. Any field that is NaN at either end stays NaN. Returns false
// when `press` falls outside the sounding.
bool soundingAt(const analysis::Sounding& s, double press, analysis::SoundingLevel& out) {
    if (s.levels.size() < 2) return false;
    for (std::size_t i = 0; i + 1 < s.levels.size(); ++i) {
        const analysis::SoundingLevel& a = s.levels[i];      // levels run top -> bottom
        const analysis::SoundingLevel& b = s.levels[i + 1];
        if (press < a.pressure || press > b.pressure) continue;
        const double denom = std::log(b.pressure) - std::log(a.pressure);
        const double f = denom != 0.0 ? (std::log(press) - std::log(a.pressure)) / denom : 0.0;
        auto mix = [f](float x, float y) {
            return std::isnan(x) || std::isnan(y) ? std::numeric_limits<float>::quiet_NaN()
                                                  : static_cast<float>(std::lerp(x, y, f));
        };
        out.pressure = press;
        out.tempK = mix(a.tempK, b.tempK);
        out.dewpointK = mix(a.dewpointK, b.dewpointK);
        out.windU = mix(a.windU, b.windU);
        out.windV = mix(a.windV, b.windV);
        return true;
    }
    return false;
}
}  // namespace

double SkewTView::Layout::yOfP(double press) const {
    const double logTop = std::log(kPtop), logBot = std::log(kPbot);
    return rect.top() + rect.height() * (std::log(press) - logTop) / (logBot - logTop);
}

double SkewTView::Layout::pOfY(double y) const {
    const double logTop = std::log(kPtop), logBot = std::log(kPbot);
    return std::exp(logTop + (y - rect.top()) / rect.height() * (logBot - logTop));
}

double SkewTView::Layout::xOfT(double tC, double y) const {
    const double base = rect.left() + (tC - kTmin) / (kTmax - kTmin) * rect.width();
    return base + kSkew * (rect.bottom() - y);
}

double SkewTView::Layout::tOfX(double x, double y) const {
    const double base = x - kSkew * (rect.bottom() - y);
    return kTmin + (base - rect.left()) / rect.width() * (kTmax - kTmin);
}

SkewTView::Layout SkewTView::layout() const {
    Layout lay;
    lay.rect = QRectF(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    lay.valid = lay.rect.width() >= 2 && lay.rect.height() >= 2;
    return lay;
}

SkewTView::SkewTView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 420);
    setMouseTracking(true);
    connect(&HoverOptions::instance(), &HoverOptions::changed, this, [this](HoverView v) {
        if (v != HoverView::SkewT) return;
        hoverActive_ = false;  // drop a badge left over from before the toggle
        update();
    });
}

void SkewTView::setSounding(const analysis::Sounding& s) {
    s_ = s;
    update();
}

void SkewTView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const Layout lay = layout();
    const QRectF& r = lay.rect;
    p.setClipRect(r);

    auto yOfP = [&](double press) { return lay.yOfP(press); };
    auto xOfT = [&](double tC, double y) { return lay.xOfT(tC, y); };

    // Isotherms (skewed straight lines).
    p.setPen(QPen(QColor(200, 120, 120, 120), 0.6));
    for (double t = -120; t <= kTmax; t += 10) {
        p.drawLine(QPointF(xOfT(t, r.bottom()), r.bottom()), QPointF(xOfT(t, r.top()), r.top()));
    }

    // Dry adiabats (theta const): T(K) = theta * (p/1000)^0.2854.
    p.setPen(QPen(QColor(120, 160, 120, 120), 0.6));
    for (double thetaC = -30; thetaC <= 160; thetaC += 10) {
        const double theta = thetaC + 273.15;
        QPainterPath path;
        bool first = true;
        for (double press = kPbot; press >= kPtop; press -= 25) {
            const double tK = theta * std::pow(press / 1000.0, 0.2854);
            const QPointF pt(xOfT(tK - 273.15, yOfP(press)), yOfP(press));
            if (first) { path.moveTo(pt); first = false; } else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // Saturation mixing-ratio lines (dashed): es = w*p/(0.622+w), Td from es.
    QPen mr(QColor(120, 140, 170, 130), 0.6);
    mr.setStyle(Qt::DashLine);
    p.setPen(mr);
    for (double wg : {1.0, 2.0, 4.0, 8.0, 16.0, 32.0}) {
        const double w = wg / 1000.0;  // kg/kg
        QPainterPath path;
        bool first = true;
        for (double press = kPbot; press >= 300; press -= 25) {
            const double es = w * press / (0.622 + w);
            const double td = tempForEs(es);
            const QPointF pt(xOfT(td, yOfP(press)), yOfP(press));
            if (first) { path.moveTo(pt); first = false; } else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // Isobars.
    p.setClipping(false);
    p.setPen(QColor(120, 120, 120));
    for (double press : {1000.0, 850.0, 700.0, 500.0, 300.0, 200.0, 100.0}) {
        const double y = yOfP(press);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.drawText(QRectF(0, y - 8, kML - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(press, 'g', 4));
    }
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Temperature-axis labels along the bottom.
    for (double t = kTmin; t <= kTmax; t += 20) {
        const double x = xOfT(t, r.bottom());
        p.drawText(QRectF(x - 20, r.bottom() + 4, 40, 16), Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(t, 'g', 3) + "°");
    }

    // Sounding traces.
    p.setClipRect(r);
    auto drawTrace = [&](bool dewpoint, QColor color) {
        QPainterPath path;
        bool first = true;
        for (const auto& lvl : s_.levels) {
            const float k = dewpoint ? lvl.dewpointK : lvl.tempK;
            if (std::isnan(k)) continue;
            const QPointF pt(xOfT(k - 273.15f, yOfP(lvl.pressure)), yOfP(lvl.pressure));
            if (first) { path.moveTo(pt); first = false; } else path.lineTo(pt);
        }
        p.setPen(QPen(color, 2.0));
        p.drawPath(path);
    };
    if (!s_.levels.empty()) {
        drawTrace(true, QColor(30, 140, 60));    // dewpoint (green)
        drawTrace(false, QColor(200, 40, 40));   // temperature (red)
    }
    p.setClipping(false);

    // Wind profile: a column of barbs down the right gutter (thinned so they don't
    // overlap), one per level with earth-relative U/V data.
    const bool hasWind = std::any_of(s_.levels.begin(), s_.levels.end(), [](const auto& l) {
        return !std::isnan(l.windU) && !std::isnan(l.windV);
    });
    if (hasWind) {
        const double barbX = r.right() + 24;
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(barbX - 16, r.top() - 14, 32, 12), Qt::AlignHCenter | Qt::AlignBottom,
                   tr("kt"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(palette().color(QPalette::Text), 1.0));
        p.setBrush(palette().color(QPalette::Text));
        double lastY = -1e9;
        for (const auto& lvl : s_.levels) {
            if (std::isnan(lvl.windU) || std::isnan(lvl.windV)) continue;
            const double y = yOfP(lvl.pressure);
            if (y - lastY < 20.0) continue;  // keep barbs from overlapping
            lastY = y;
            const double speed = std::hypot(lvl.windU, lvl.windV);
            const render::WindBarb barb = render::makeWindBarb(
                QPointF(barbX, y), QPointF(-lvl.windU, lvl.windV), analysis::toKnots(speed), 16.0);
            if (barb.calm) {
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(barbX, y), 2.5, 2.5);
                p.setBrush(palette().color(QPalette::Text));
                continue;
            }
            for (const QLineF& l : barb.lines) p.drawLine(l);
            for (const QPolygonF& tri : barb.pennants) p.drawPolygon(tri);
        }
        p.setBrush(Qt::NoBrush);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // Legend (top-left, translucent so it stays readable over the background grid).
    if (!s_.levels.empty()) {
        struct Item { QColor color; Qt::PenStyle style; double w; QString label; };
        const std::vector<Item> items = {
            {QColor(200, 40, 40), Qt::SolidLine, 2.0, tr("Temperature")},
            {QColor(30, 140, 60), Qt::SolidLine, 2.0, tr("Dewpoint")},
            {QColor(120, 160, 120), Qt::SolidLine, 0.8, tr("Dry adiabat")},
            {QColor(120, 140, 170), Qt::DashLine, 0.8, tr("Mixing ratio")},
            {QColor(200, 120, 120), Qt::SolidLine, 0.8, tr("Isotherm")},
        };
        const QFontMetrics fm(p.font());
        int textW = 0;
        for (const auto& it : items) textW = std::max(textW, fm.horizontalAdvance(it.label));
        const int rowH = fm.height() + 2;
        const int sample = 22, padX = 6, gap = 6;
        const QRectF box(r.left() + 6, r.top() + 6, padX * 2 + sample + gap + textW,
                         padX * 2 + rowH * static_cast<int>(items.size()));
        QColor bg = palette().color(QPalette::Base);
        bg.setAlpha(215);
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        p.setBrush(bg);
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        double yy = box.top() + padX + rowH / 2.0;
        for (const auto& it : items) {
            QPen pen(it.color, it.w);
            pen.setStyle(it.style);
            p.setPen(pen);
            const double lx = box.left() + padX;
            p.drawLine(QPointF(lx, yy), QPointF(lx + sample, yy));
            p.setPen(palette().color(QPalette::Text));
            p.drawText(QRectF(lx + sample + gap, yy - rowH / 2.0, textW + 2, rowH),
                       Qt::AlignLeft | Qt::AlignVCenter, it.label);
            yy += rowH;
        }
    }

    if (s_.levels.empty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Pick a sounding point on the map"));
    } else {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, 2, width(), kMT - 4), Qt::AlignCenter,
                   tr("Skew-T  (%1°, %2°)")
                       .arg(s_.point.lat, 0, 'f', 1)
                       .arg(s_.point.lon, 0, 'f', 1));
    }

    if (hoverActive_) paintHoverReadout(p, r, hoverPos_, hoverLines_, palette());
}

void SkewTView::mouseMoveEvent(QMouseEvent* event) {
    const bool wasActive = hoverActive_;
    hoverActive_ = false;
    const Layout lay = layout();
    const QPointF pos = event->position();
    if (!lay.valid || !lay.rect.contains(pos) || s_.levels.empty() ||
        !HoverOptions::instance().enabled(HoverView::SkewT)) {
        if (wasActive) update();
        return;
    }

    // Where the cursor sits on the diagram, then what the sounding says at that
    // pressure — the second is the useful number, the first tells you which
    // isotherm/isobar you are reading against.
    const double press = lay.pOfY(pos.y());
    const double tC = lay.tOfX(pos.x(), pos.y());
    QStringList lines;
    lines << QStringLiteral("%1 hPa   %2 °C").arg(press, 0, 'f', 0).arg(tC, 0, 'f', 1);

    analysis::SoundingLevel lvl{};
    if (soundingAt(s_, press, lvl)) {
        if (!std::isnan(lvl.tempK)) {
            QString s = QStringLiteral("T %1 °C").arg(lvl.tempK - 273.15f, 0, 'f', 1);
            if (!std::isnan(lvl.dewpointK))
                s += QStringLiteral("   Td %1 °C").arg(lvl.dewpointK - 273.15f, 0, 'f', 1);
            lines << s;
        }
        if (!std::isnan(lvl.windU) && !std::isnan(lvl.windV)) {
            // Meteorological convention: the direction the wind blows *from*.
            const double dir = std::fmod(
                std::atan2(-lvl.windU, -lvl.windV) * 180.0 / std::numbers::pi + 360.0, 360.0);
            const double kt = analysis::toKnots(std::hypot(lvl.windU, lvl.windV));
            lines << QStringLiteral("Wind %1°  %2 kt").arg(dir, 0, 'f', 0).arg(kt, 0, 'f', 0);
        }
    }

    hoverActive_ = true;
    hoverPos_ = pos;
    hoverLines_ = lines;
    update();
}

void SkewTView::leaveEvent(QEvent*) {
    if (!hoverActive_) return;
    hoverActive_ = false;
    update();
}

}  // namespace met::app
