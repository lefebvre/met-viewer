#include "viewer/app/skewtview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
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
// The conventional skew-T frame: a 100-1050 hPa log-pressure axis read against a fixed
// -40...40 °C window, with isotherms skewed 0.85 px of x per px of plot height.
constexpr double kPtopStd = 100.0, kPbotStd = 1050.0;  // pressure axis (hPa)
constexpr double kTminStd = -40.0, kTmaxStd = 40.0;    // temperature at the bottom (°C)
constexpr double kSkewStd = 0.85;                      // px of x per px of height
// How far fitFrame may go to hold a sounding the standard frame cannot.
constexpr double kSkewFloor = 0.25;  // below this it stops reading as a skew-T
constexpr double kTSpanMax = 400.0;  // widest temperature window (°C)
constexpr double kFitMargin = 6.0;   // px kept clear inside the side edges
constexpr int kCurveSteps = 80;      // vertices per background curve, spaced in log-p

// The isobars that get a line and a label — and, when the sounding carries
// heights, the pressures whose altitude is labelled inside the diagram. The
// mandatory levels down to 100 hPa, then the standard stratospheric ladder for
// soundings that reach above it; only those inside the frame are drawn.
constexpr double kIsobars[] = {1000.0, 850.0, 700.0, 500.0, 300.0, 200.0, 100.0, 70.0, 50.0,
                               30.0,   20.0,  10.0,  7.0,   5.0,   3.0,   2.0,   1.0};

// The frame a sounding is drawn in: its log-pressure span, the temperature window at
// the bottom of the diagram, and the skew. Mirrors the frame fields of
// SkewTView::Layout, which are private to the class and so out of reach from here.
struct Frame {
    double pTop = kPtopStd, pBot = kPbotStd;
    double tMin = kTminStd, tMax = kTmaxStd;
    double skew = kSkewStd;
};

// Where `press` sits down the frame: 0 at the top, 1 at the bottom.
double pressFrac(double pTop, double pBot, double press) {
    const double span = std::log(pBot) - std::log(pTop);
    return span != 0.0 ? (std::log(press) - std::log(pTop)) / span : 0.0;
}

// The i-th vertex of a background curve running from `pBot` up to `pTop`, spaced evenly
// in log-p so the curve stays smooth however many decades the frame spans.
double curvePress(double pTop, double pBot, int i) {
    const double f = static_cast<double>(i) / kCurveSteps;
    return std::exp(std::log(pBot) + f * (std::log(pTop) - std::log(pBot)));
}

// Every finite temperature and dewpoint in the sounding, paired with its pressure:
// the points the frame has to hold.
std::vector<std::pair<double, double>> framePoints(const analysis::Sounding& s) {
    std::vector<std::pair<double, double>> pts;
    pts.reserve(s.levels.size() * 2);
    for (const analysis::SoundingLevel& lvl : s.levels) {
        if (!(lvl.pressure > 0.0)) continue;
        if (!std::isnan(lvl.tempK)) pts.emplace_back(lvl.pressure, lvl.tempK - 273.15);
        if (!std::isnan(lvl.dewpointK)) pts.emplace_back(lvl.pressure, lvl.dewpointK - 273.15);
    }
    return pts;
}

// Slide the temperature window so the points sit centred across the plot, keeping the
// given skew and window width. False when no placement of that window fits them.
bool centreWindow(const std::vector<std::pair<double, double>>& pts, double skew, double tSpan,
                  double w, double h, Frame& f) {
    double qLo = std::numeric_limits<double>::infinity(), qHi = -qLo;
    for (const auto& pt : pts) {
        // x, but for the unknown tMin, which shifts every point by the same amount.
        const double q =
            pt.second / tSpan * w + skew * h * (1.0 - pressFrac(f.pTop, f.pBot, pt.first));
        qLo = std::min(qLo, q);
        qHi = std::max(qHi, q);
    }
    const double room = w - 2.0 * kFitMargin;
    if (!std::isfinite(qLo) || qHi - qLo > room) return false;
    f.skew = skew;
    f.tMin = (qLo - (kFitMargin + (room - (qHi - qLo)) / 2.0)) * tSpan / w;
    f.tMax = f.tMin + tSpan;
    return true;
}

// The frame to draw `s` in, on a plot `w` x `h` px.
//
// A skew-T is read by eye against fixed isotherms, so the conventional window and skew
// are kept whenever they hold the sounding — which is every sounding that stops at
// 100 hPa, and most that reach 10 hPa. Higher than that the geometry runs out. The skew
// shifts the top of the frame right by 0.85x the plot height, slightly more than the
// plot is wide, and that is only affordable while the air up there is cold enough to
// start far enough left. At a 1 hPa top the stratopause is near 0 °C and lands some
// 220 px past the right edge, where the clip in paintEvent drops it: the level is in
// the sounding but not on the diagram, which is the whole complaint.
//
// Two knobs can buy the room back, and the skew is the one to turn. Widening the
// temperature window cannot help at all once the span of the skew exceeds the plot
// width — no window is wide enough — and before that it pays in exactly the detail the
// diagram exists to show: at a 5 hPa top the troposphere squeezes from 357 px to 272 px
// of a 398 px plot. Relaxing the skew always works and costs no temperature resolution,
// so it goes first. The window widens only for a sounding whose raw temperature span
// does not fit in 80 °C at all — a tropical surface over a -80 °C tropopause — which no
// amount of skew can fix.
Frame fitFrame(const analysis::Sounding& s, double w, double h) {
    Frame f;
    const std::vector<std::pair<double, double>> pts = framePoints(s);
    // Extend to the data; never shrink below the conventional frame.
    for (const auto& pt : pts) {
        f.pTop = std::min(f.pTop, pt.first);
        f.pBot = std::max(f.pBot, pt.first);
    }
    if (pts.empty() || w <= 0.0 || h <= 0.0) return f;

    const bool standardFits = std::all_of(pts.begin(), pts.end(), [&](const auto& pt) {
        const double x = (pt.second - f.tMin) / (f.tMax - f.tMin) * w +
                         f.skew * h * (1.0 - pressFrac(f.pTop, f.pBot, pt.first));
        return x >= kFitMargin && x <= w - kFitMargin;
    });
    if (standardFits) return f;

    for (double skew = kSkewStd; skew >= kSkewFloor; skew -= 0.01)
        if (centreWindow(pts, skew, kTmaxStd - kTminStd, w, h, f)) return f;
    for (double tSpan = kTmaxStd - kTminStd; tSpan <= kTSpanMax; tSpan += 10.0)
        if (centreWindow(pts, kSkewFloor, tSpan, w, h, f)) return f;

    // Nothing holds it. Take the widest frame centred on the data and let the clip drop
    // whatever still falls outside — better than a frame centred on nothing.
    double tLo = std::numeric_limits<double>::infinity(), tHi = -tLo;
    for (const auto& pt : pts) {
        tLo = std::min(tLo, pt.second);
        tHi = std::max(tHi, pt.second);
    }
    f.skew = kSkewFloor;
    f.tMin = 0.5 * (tLo + tHi) - kTSpanMax / 2.0;
    f.tMax = f.tMin + kTSpanMax;
    return f;
}

// The isobars inside the frame, top-down.
std::vector<double> isobarsIn(double pTop, double pBot) {
    std::vector<double> out;
    for (double press : kIsobars)
        if (press >= pTop && press <= pBot) out.push_back(press);
    return out;
}

// A tick step that divides a `tSpan`-wide window into at most `maxCount` intervals.
double niceStep(double tSpan, double maxCount) {
    for (double step : {5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0})
        if (tSpan / step <= maxCount) return step;
    return 250.0;
}

// The dry adiabats worth drawing: the conventional 10 K ladder, then a geometric
// continuation for a frame that reaches into the stratosphere. Every adiabat on the
// ladder has left the frame by roughly 90 hPa, so without the continuation the top of
// an extended diagram is bare; and because adiabats crowd together as pressure falls,
// continuing the ladder linearly instead would pack that same region solid.
// `tTopRight` is the warmest temperature the top row of the frame can show, so the
// list stops as soon as it covers that corner.
std::vector<double> dryAdiabats(double pTop, double tTopRight) {
    std::vector<double> thetas;
    for (double c = -30.0; c <= 160.0; c += 10.0) thetas.push_back(c + 273.15);
    const double need = (tTopRight + 273.15) * std::pow(1000.0 / pTop, 0.2854);
    for (double theta = thetas.back() * 1.12; theta <= need; theta *= 1.12) thetas.push_back(theta);
    return thetas;
}

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
        const analysis::SoundingLevel& a = s.levels[i];  // levels run top -> bottom
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
        // Height interpolates in log-p like everything else here, which is exactly
        // the hypsometric relation for a layer of constant mean temperature.
        out.heightGpm = mix(a.heightGpm, b.heightGpm);
        return true;
    }
    return false;
}

// One legend entry: a line sample and its label.
struct LegendItem {
    QColor color;
    Qt::PenStyle style;
    double w;
    QString label;
};

// The legend's entries and the box they occupy. The box is wanted before the
// legend is drawn — the height labels down the left edge check it so a label
// cannot end up hidden underneath — so sizing lives here rather than inline.
struct Legend {
    std::vector<LegendItem> items;
    QRectF box;
    int textW = 0;
    int rowH = 0;
    static constexpr int kSample = 22, kPad = 6, kGap = 6;
};

Legend legendFor(const QFontMetrics& fm, const QRectF& r) {
    Legend leg;
    leg.items = {
        {QColor(200, 40, 40), Qt::SolidLine, 2.0, SkewTView::tr("Temperature")},
        {QColor(30, 140, 60), Qt::SolidLine, 2.0, SkewTView::tr("Dewpoint")},
        {QColor(120, 160, 120), Qt::SolidLine, 0.8, SkewTView::tr("Dry adiabat")},
        {QColor(120, 140, 170), Qt::DashLine, 0.8, SkewTView::tr("Mixing ratio")},
        {QColor(200, 120, 120), Qt::SolidLine, 0.8, SkewTView::tr("Isotherm")},
    };
    for (const auto& it : leg.items)
        leg.textW = std::max(leg.textW, fm.horizontalAdvance(it.label));
    leg.rowH = fm.height() + 2;
    leg.box = QRectF(r.left() + 6, r.top() + 6,
                     Legend::kPad * 2 + Legend::kSample + Legend::kGap + leg.textW,
                     Legend::kPad * 2 + leg.rowH * static_cast<int>(leg.items.size()));
    return leg;
}
}  // namespace

double SkewTView::Layout::yOfP(double press) const {
    const double logTop = std::log(pTop), logBot = std::log(pBot);
    return rect.top() + rect.height() * (std::log(press) - logTop) / (logBot - logTop);
}

double SkewTView::Layout::pOfY(double y) const {
    const double logTop = std::log(pTop), logBot = std::log(pBot);
    return std::exp(logTop + (y - rect.top()) / rect.height() * (logBot - logTop));
}

double SkewTView::Layout::xOfT(double tC, double y) const {
    const double base = rect.left() + (tC - tMin) / (tMax - tMin) * rect.width();
    return base + skew * (rect.bottom() - y);
}

double SkewTView::Layout::tOfX(double x, double y) const {
    const double base = x - skew * (rect.bottom() - y);
    return tMin + (base - rect.left()) / rect.width() * (tMax - tMin);
}

SkewTView::Layout SkewTView::layout() const {
    Layout lay;
    lay.rect = QRectF(kML, kMT, width() - kML - kMR, height() - kMT - kMB);
    lay.valid = lay.rect.width() >= 2 && lay.rect.height() >= 2;
    const Frame f = fitFrame(s_, lay.rect.width(), lay.rect.height());
    lay.pTop = f.pTop;
    lay.pBot = f.pBot;
    lay.tMin = f.tMin;
    lay.tMax = f.tMax;
    lay.skew = f.skew;
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
    emit heightsAvailableChanged(hasHeights());
    update();
}

void SkewTView::setHeightLabelsEnabled(bool on) {
    if (showHeights_ == on) return;
    showHeights_ = on;
    update();
}

bool SkewTView::hasHeights() const {
    return std::any_of(s_.levels.begin(), s_.levels.end(),
                       [](const auto& l) { return !std::isnan(l.heightGpm); });
}

void SkewTView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const Layout lay = layout();
    const QRectF& r = lay.rect;
    p.setClipRect(r);

    auto yOfP = [&](double press) { return lay.yOfP(press); };
    auto xOfT = [&](double tC, double y) { return lay.xOfT(tC, y); };

    // Isotherms (skewed straight lines). They lean right going up, so the coldest one
    // that can reach the frame is the temperature at its top-left corner, not tMin.
    const double isoStep = niceStep(lay.tMax - lay.tMin, 8.0);
    p.setPen(QPen(QColor(200, 120, 120, 120), 0.6));
    for (double t = std::floor(lay.tOfX(r.left(), r.top()) / isoStep) * isoStep; t <= lay.tMax;
         t += isoStep) {
        p.drawLine(QPointF(xOfT(t, r.bottom()), r.bottom()), QPointF(xOfT(t, r.top()), r.top()));
    }

    // Dry adiabats (theta const): T(K) = theta * (p/1000)^0.2854.
    p.setPen(QPen(QColor(120, 160, 120, 120), 0.6));
    for (double theta : dryAdiabats(lay.pTop, lay.tOfX(r.right(), r.top()))) {
        QPainterPath path;
        for (int i = 0; i <= kCurveSteps; ++i) {
            const double press = curvePress(lay.pTop, lay.pBot, i);
            const double tK = theta * std::pow(press / 1000.0, 0.2854);
            const QPointF pt(xOfT(tK - 273.15, yOfP(press)), yOfP(press));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // Saturation mixing-ratio lines (dashed): es = w*p/(0.622+w), Td from es. They stop
    // at 300 hPa, above which there is too little vapour for them to mean anything.
    QPen mr(QColor(120, 140, 170, 130), 0.6);
    mr.setStyle(Qt::DashLine);
    p.setPen(mr);
    const double mrTop = std::max(300.0, lay.pTop);
    for (double wg : {1.0, 2.0, 4.0, 8.0, 16.0, 32.0}) {
        const double w = wg / 1000.0;  // kg/kg
        QPainterPath path;
        for (int i = 0; i <= kCurveSteps; ++i) {
            const double press = curvePress(mrTop, lay.pBot, i);
            const double es = w * press / (0.622 + w);
            const double td = tempForEs(es);
            const QPointF pt(xOfT(td, yOfP(press)), yOfP(press));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // Isobars.
    const std::vector<double> isobars = isobarsIn(lay.pTop, lay.pBot);
    p.setClipping(false);
    p.setPen(QColor(120, 120, 120));
    for (double press : isobars) {
        const double y = yOfP(press);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.drawText(QRectF(0, y - 8, kML - 4, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(press, 'g', 4));
    }
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(r);

    // Geopotential height against the pressure axis: each labelled isobar also
    // gets the altitude this sounding puts it at, just inside the diagram. Drawn
    // only when the dataset carried a height field — an altitude inferred from
    // the temperature trace alone would need a surface height nobody supplied.
    const Legend leg = legendFor(QFontMetrics(p.font()), r);
    if (showHeights_ && hasHeights()) {
        p.setPen(QColor(95, 125, 170));
        for (double press : isobars) {
            analysis::SoundingLevel lvl{};
            if (!soundingAt(s_, press, lvl) || std::isnan(lvl.heightGpm)) continue;
            const double y = yOfP(press);
            // Sit on top of the isobar, except at the top of the diagram where that
            // would put the label outside the frame; step aside for the legend
            // rather than dropping the label it happens to land behind.
            QRectF box(r.left() + 4, y - 16 < r.top() ? y + 1 : y - 16, 60, 15);
            if (box.intersects(leg.box)) box.moveLeft(leg.box.right() + 6);
            p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, formatHeight(lvl.heightGpm));
        }
    }

    // Temperature-axis labels along the bottom.
    const double labStep = niceStep(lay.tMax - lay.tMin, 4.0);
    for (double t = std::ceil(lay.tMin / labStep) * labStep; t <= lay.tMax; t += labStep) {
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
            if (first) {
                path.moveTo(pt);
                first = false;
            } else path.lineTo(pt);
        }
        p.setPen(QPen(color, 2.0));
        p.drawPath(path);
    };
    if (!s_.levels.empty()) {
        drawTrace(true, QColor(30, 140, 60));   // dewpoint (green)
        drawTrace(false, QColor(200, 40, 40));  // temperature (red)
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
        QColor bg = palette().color(QPalette::Base);
        bg.setAlpha(215);
        p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        p.setBrush(bg);
        p.drawRect(leg.box);
        p.setBrush(Qt::NoBrush);
        double yy = leg.box.top() + Legend::kPad + leg.rowH / 2.0;
        for (const auto& it : leg.items) {
            QPen pen(it.color, it.w);
            pen.setStyle(it.style);
            p.setPen(pen);
            const double lx = leg.box.left() + Legend::kPad;
            p.drawLine(QPointF(lx, yy), QPointF(lx + Legend::kSample, yy));
            p.setPen(palette().color(QPalette::Text));
            p.drawText(QRectF(lx + Legend::kSample + Legend::kGap, yy - leg.rowH / 2.0,
                              leg.textW + 2, leg.rowH),
                       Qt::AlignLeft | Qt::AlignVCenter, it.label);
            yy += leg.rowH;
        }
    }

    if (s_.levels.empty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, tr("Pick a sounding point on the map"));
    } else {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(0, 2, width(), kMT - 4), Qt::AlignCenter,
                   tr("Skew-T  (%1°, %2°)")
                       .arg(s_.point.lat, 0, 'f', coordPrec_)
                       .arg(s_.point.lon, 0, 'f', coordPrec_));
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
        if (!std::isnan(lvl.heightGpm))
            lines << QStringLiteral("Z %1 m").arg(lvl.heightGpm, 0, 'f', 0);
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
