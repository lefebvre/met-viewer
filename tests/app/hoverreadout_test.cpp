#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointF>
#include <QRegularExpression>
#include <Qt>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/app/crosssectionview.h"
#include "viewer/app/hoverreadout.h"
#include "viewer/app/plotview2d.h"
#include "viewer/app/skewtview.h"
#include "viewer/app/timeseriesview.h"
#include "viewer/core/field.h"
#include "viewer/core/grid.h"

using namespace met;

namespace {

// Render a widget to a white-backed image, the same way skewtview_test does.
QImage snapshot(QWidget& w) {
    QImage img(w.size(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    w.render(&img);
    return img;
}

// Deliver a synthetic mouse-move at widget coordinates. The views read
// QMouseEvent::position(), so the local position is what matters.
void moveMouse(QWidget& w, QPointF pos) {
    QMouseEvent ev(QEvent::MouseMove, pos, w.mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QApplication::sendEvent(&w, &ev);
}

void leaveMouse(QWidget& w) {
    QEvent ev(QEvent::Leave);
    QApplication::sendEvent(&w, &ev);
}

// Pixels that differ between two renders of the same widget. The badge is opaque
// and several rows tall, so a drawn readout moves hundreds of pixels.
int changedPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
}

// A smooth 40x30 lat/lon field over 30..60N, -110..-70E.
std::shared_ptr<core::Field2D> makeField() {
    core::RegularLatLonGrid g;
    g.lat0 = 60.0;
    g.lon0 = -110.0;
    g.dlat = -1.0;
    g.dlon = 1.0;
    g.nlon = 40;
    g.nlat = 30;
    auto f = std::make_shared<core::Field2D>();
    f->grid = g;
    f->meta.units = "K";
    f->values.resize(static_cast<std::size_t>(g.nlon) * static_cast<std::size_t>(g.nlat));
    for (int j = 0; j < g.nlat; ++j)
        for (int i = 0; i < g.nlon; ++i)
            f->values[static_cast<std::size_t>(j * g.nlon + i)] =
                273.0f + static_cast<float>(i + j) * 0.5f;
    return f;
}

analysis::CrossSection makeSection() {
    analysis::CrossSection cs;
    cs.units = "K";
    const int ns = 60;
    const std::vector<double> levels = {250, 400, 500, 700, 850, 1000};
    for (int s = 0; s < ns; ++s) {
        cs.points.push_back({40.0 + 0.1 * s, -100.0 + 0.2 * s});
        cs.distancesKm.push_back(20.0 * s);
    }
    for (double press : levels) {
        cs.pressures.emplace_back(ns, press);
        std::vector<float> row(ns);
        for (int s = 0; s < ns; ++s)
            row[static_cast<std::size_t>(s)] = static_cast<float>(300.0 - press * 0.05 + s * 0.1);
        cs.values.push_back(std::move(row));
    }
    return cs;
}

analysis::TimeSeries makeSeries() {
    analysis::TimeSeries ts;
    ts.point = {45.0, -95.0};
    ts.units = "K";
    for (int i = 0; i < 12; ++i) {
        ts.times.push_back(core::TimePoint{1'700'000'000 + i * 3600});
        ts.values.push_back(280.0f + static_cast<float>(i));
    }
    return ts;
}

analysis::Sounding makeSounding() {
    analysis::Sounding s;
    s.point = {45.0, -95.0};
    for (double p : {250.0, 400.0, 500.0, 700.0, 850.0, 1000.0}) {
        analysis::SoundingLevel lvl;
        lvl.pressure = p;
        lvl.tempK = 273.0f - static_cast<float>((1000.0 - p) * 0.02);
        lvl.dewpointK = lvl.tempK - 5.0f;
        lvl.windU = 15.0f;
        lvl.windV = 10.0f;
        s.levels.push_back(lvl);
    }
    return s;
}

// Restores a HoverView's flag on scope exit so one test's toggle can't leak into
// another — HoverOptions is process-wide.
class ScopedHover {
public:
    explicit ScopedHover(app::HoverView v) : v_(v), was_(app::HoverOptions::instance().enabled(v)) {}
    ~ScopedHover() { app::HoverOptions::instance().setEnabled(v_, was_); }

private:
    app::HoverView v_;
    bool was_;
};

}  // namespace

TEST(FormatValueWithUnits, ConvertsToThePreferredDisplayUnit) {
    EXPECT_EQ(app::formatValueWithUnits(273.15, "K"), QStringLiteral("273.15 K (0.00 °C)"));
    EXPECT_EQ(app::formatValueWithUnits(100000.0, "Pa"),
              QStringLiteral("100000.00 Pa (1000.00 hPa)"));
    // core::preferredDisplayUnit only offers an alternative for K and Pa; everything
    // else — and a unitless value — prints bare.
    EXPECT_EQ(app::formatValueWithUnits(10.0, "m/s"), QStringLiteral("10.00 m/s"));
    EXPECT_EQ(app::formatValueWithUnits(4.5, ""), QStringLiteral("4.50"));
}

TEST(CoordPrecision, ThreeDigitsOnlyForHighResolutionGrids) {
    // Synoptic-scale spacing -> 2 digits; convection-allowing (< ~5 km) -> 3.
    EXPECT_EQ(app::coordPrecision(0.25), 2);   // ERA5 / GFS
    EXPECT_EQ(app::coordPrecision(0.1), 2);
    EXPECT_EQ(app::coordPrecision(0.027), 3);  // HRRR ~3 km
    EXPECT_EQ(app::coordPrecision(0.0), 2);    // unknown spacing

    core::RegularLatLonGrid coarse;
    coarse.dlat = -0.25;
    coarse.dlon = 0.25;
    EXPECT_EQ(app::coordPrecision(core::gridSpacingDeg(core::GridDef{coarse})), 2);

    core::ProjectedGrid fine;
    fine.crs = core::Crs("+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=-97.5 +R=6371200 "
                         "+units=m");
    fine.dx = fine.dy = 3000.0;  // 3 km
    EXPECT_EQ(app::coordPrecision(core::gridSpacingDeg(core::GridDef{fine})), 3);
}

TEST(HoverReadout, PlotViewDrawsBadgeOnHoverAndClearsOnLeave) {
    app::PlotView2D view;
    view.resize(640, 480);
    view.setField(makeField());

    const QImage plain = snapshot(view);
    moveMouse(view, QPointF(320, 240));
    const QImage hovered = snapshot(view);
    EXPECT_GT(changedPixels(plain, hovered), 300);

    leaveMouse(view);
    EXPECT_EQ(changedPixels(plain, snapshot(view)), 0);
}

TEST(HoverReadout, PlotViewIgnoresHoverWhenDisabled) {
    ScopedHover restore(app::HoverView::Plot);
    app::PlotView2D view;
    view.resize(640, 480);
    view.setField(makeField());

    const QImage plain = snapshot(view);
    app::HoverOptions::instance().setEnabled(app::HoverView::Plot, false);
    moveMouse(view, QPointF(320, 240));
    EXPECT_EQ(changedPixels(plain, snapshot(view)), 0);

    // Re-enabling brings the badge back for the next move.
    app::HoverOptions::instance().setEnabled(app::HoverView::Plot, true);
    moveMouse(view, QPointF(320, 240));
    EXPECT_GT(changedPixels(plain, snapshot(view)), 300);
}

TEST(HoverReadout, CrossSectionViewDrawsBadgeOnHover) {
    app::CrossSectionView view;
    view.resize(700, 420);
    view.setSection(makeSection());

    const QImage plain = snapshot(view);
    moveMouse(view, QPointF(350, 210));
    EXPECT_GT(changedPixels(plain, snapshot(view)), 300);

    leaveMouse(view);
    EXPECT_EQ(changedPixels(plain, snapshot(view)), 0);
}

TEST(HoverReadout, TimeSeriesViewDrawsBadgeOnHover) {
    app::TimeSeriesView view;
    view.resize(640, 320);
    view.setSeries(makeSeries(), QStringLiteral("t"));

    const QImage plain = snapshot(view);
    moveMouse(view, QPointF(320, 160));
    EXPECT_GT(changedPixels(plain, snapshot(view)), 300);

    leaveMouse(view);
    EXPECT_EQ(changedPixels(plain, snapshot(view)), 0);
}

TEST(HoverReadout, SkewTViewDrawsBadgeOnHover) {
    app::SkewTView view;
    view.resize(500, 560);
    view.setSounding(makeSounding());

    const QImage plain = snapshot(view);
    moveMouse(view, QPointF(250, 280));
    EXPECT_GT(changedPixels(plain, snapshot(view)), 300);

    leaveMouse(view);
    EXPECT_EQ(changedPixels(plain, snapshot(view)), 0);
}

// The badge's numbers come from inverting each view's paint transform, so check
// them against a position whose expected value we can compute independently.
TEST(HoverReadout, SkewTReportsThePressureItsOwnAxisDrawsAtThatRow) {
    app::SkewTView view;
    view.resize(500, 560);
    view.setSounding(makeSounding());

    // Reproduce the paint-side log-p mapping: rect is inset by the view's margins
    // (44 left, 58 right, 24 top, 30 bottom) over the 100..1050 hPa axis.
    const double top = 24.0, height = 560.0 - 24.0 - 30.0;
    const double logTop = std::log(100.0), logBot = std::log(1050.0);
    const double yOf500 = top + height * (std::log(500.0) - logTop) / (logBot - logTop);

    moveMouse(view, QPointF(250, yOf500));
    const QStringList lines = view.hoverText();
    ASSERT_FALSE(lines.isEmpty());
    EXPECT_NEAR(lines[0].split(' ').first().toDouble(), 500.0, 2.0) << lines[0].toStdString();
    // The sounding at 500 hPa is 273 - (1000-500)*0.02 = 263 K = -10.15 °C.
    ASSERT_GE(lines.size(), 2);
    EXPECT_TRUE(lines[1].startsWith(QStringLiteral("T -10."))) << lines[1].toStdString();
}

TEST(HoverReadout, CrossSectionReportsDistanceAlongThePath) {
    app::CrossSectionView view;
    view.resize(700, 420);
    view.setSection(makeSection());

    // Plot rect spans x in [56, 700-16); the section runs 0..1180 km, so the
    // midpoint of the rect is half of that.
    const double left = 56.0, right = 700.0 - 16.0;
    moveMouse(view, QPointF(0.5 * (left + right), 210));
    const QStringList lines = view.hoverText();
    ASSERT_FALSE(lines.isEmpty());
    EXPECT_NEAR(lines[0].split(' ').first().toDouble(), 590.0, 15.0) << lines[0].toStdString();
}

TEST(HoverReadout, CrossSectionHonorsCoordPrecision) {
    app::CrossSectionView view;
    view.resize(700, 420);
    view.setSection(makeSection());
    view.setCoordPrecision(3);  // pretend the source is a high-resolution grid

    moveMouse(view, QPointF(350, 210));
    const QStringList lines = view.hoverText();
    ASSERT_FALSE(lines.isEmpty());
    // "<km> km  (<lat>°, <lon>°)" — both coordinates carry three decimals.
    EXPECT_TRUE(QRegularExpression(QStringLiteral("\\(-?\\d+\\.\\d{3}°, -?\\d+\\.\\d{3}°\\)"))
                    .match(lines[0])
                    .hasMatch())
        << lines[0].toStdString();
}

TEST(HoverReadout, TimeSeriesSnapsToTheNearestSample) {
    app::TimeSeriesView view;
    view.resize(640, 320);
    view.setSeries(makeSeries(), QStringLiteral("t"));

    // 12 samples valued 280..291 K across the rect; the far left is sample 0.
    moveMouse(view, QPointF(66, 160));
    QStringList lines = view.hoverText();
    ASSERT_GE(lines.size(), 2);
    EXPECT_TRUE(lines[1].startsWith(QStringLiteral("280.00 K"))) << lines[1].toStdString();

    // The far right is the last sample, whichever pixel row we approach it from.
    moveMouse(view, QPointF(622, 60));
    lines = view.hoverText();
    ASSERT_GE(lines.size(), 2);
    EXPECT_TRUE(lines[1].startsWith(QStringLiteral("291.00 K"))) << lines[1].toStdString();
}

TEST(HoverReadout, BadgeBoxStaysInsideThePlotRectNearEveryCorner) {
    // The box prefers down-right of the cursor and flips/clamps near an edge. Paint
    // it straight onto an image so the assertion is about the box itself rather than
    // any one view's plot geometry.
    const QRectF plot(20, 20, 260, 160);
    const QStringList lines = {QStringLiteral("512 km  (44.80°, -95.20°)"),
                               QStringLiteral("685.0 hPa"),
                               QStringLiteral("273.40 K (0.25 °C)")};

    // This plot is deliberately narrower than the readout is wide, so it also covers
    // the case where placement alone cannot fit the box and the clip has to.
    for (const QPointF& at : {QPointF(24, 24), QPointF(276, 24), QPointF(24, 176),
                              QPointF(276, 176), QPointF(150, 100)}) {
        QImage img(300, 200, QImage::Format_ARGB32);
        img.fill(Qt::white);
        {
            QPainter p(&img);
            app::paintHoverReadout(p, plot, at, lines, QPalette());
        }

        int outside = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (!plot.contains(QPointF(x, y)) && img.pixel(x, y) != qRgb(255, 255, 255))
                    ++outside;
            }
        }
        EXPECT_EQ(outside, 0) << outside << " pixels painted outside the plot rect for cursor ("
                              << at.x() << ", " << at.y() << ")";
    }
}

TEST(HoverReadout, BadgeIsSkippedWhenTheCursorIsOutsideThePlotRect) {
    const QRectF plot(20, 20, 260, 160);
    QImage img(300, 200, QImage::Format_ARGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        app::paintHoverReadout(p, plot, QPointF(5, 5), {QStringLiteral("x")}, QPalette());
        app::paintHoverReadout(p, plot, QPointF(150, 100), QStringList(), QPalette());
    }
    QImage blank(300, 200, QImage::Format_ARGB32);
    blank.fill(Qt::white);
    EXPECT_EQ(changedPixels(blank, img), 0);
}
