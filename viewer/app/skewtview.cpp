#include "viewer/app/skewtview.h"

#include <cmath>

#include <QPainter>
#include <QPainterPath>

namespace met::app {
namespace {
constexpr int kML = 44, kMR = 16, kMT = 24, kMB = 30;
constexpr double kPtop = 100.0, kPbot = 1050.0;   // pressure axis (hPa)
constexpr double kTmin = -40.0, kTmax = 40.0;     // temperature at the bottom (°C)
constexpr double kSkew = 0.85;                    // px of x per px of height

// Inverse Magnus: dewpoint/temperature (°C) whose saturation vapour pressure is es (hPa).
double tempForEs(double es) {
    const double l = std::log(es / 6.112);
    return 243.12 * l / (17.62 - l);
}
}  // namespace

SkewTView::SkewTView(QWidget* parent) : QWidget(parent) { setMinimumSize(360, 420); }

void SkewTView::setSounding(const analysis::Sounding& s) {
    s_ = s;
    update();
}

void SkewTView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const QRectF r(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    p.setClipRect(r);

    const double logTop = std::log(kPtop), logBot = std::log(kPbot);
    auto yOfP = [&](double press) {
        return r.top() + r.height() * (std::log(press) - logTop) / (logBot - logTop);
    };
    auto xOfT = [&](double tC, double y) {
        const double base = r.left() + (tC - kTmin) / (kTmax - kTmin) * r.width();
        return base + kSkew * (r.bottom() - y);
    };

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
}

}  // namespace met::app
