#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QMouseEvent>
#include <QObject>
#include <QRect>
#include <QWidget>
#include <Qt>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/app/axisrange.h"
#include "viewer/app/crosssectionview.h"
#include "viewer/app/skewtview.h"
#include "viewer/app/timeseriesview.h"
#include "viewer/core/timeaxis.h"

using namespace met;

namespace {
QImage renderView(QWidget& w) {
    QImage img(w.size(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    w.render(&img);
    return img;
}

int countWhere(const QImage& img, const QRect& area, bool (*pred)(const QColor&)) {
    int n = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (pred(img.pixelColor(x, y))) ++n;
    return n;
}

// The pressure the view says is under the cursor, from its own readout, which is
// the axis inverted. Reading the axis back through the mapping the user sees beats
// asserting on a number the view merely reports.
double pressureUnderCursor(app::SkewTView& view, QPointF pos) {
    QMouseEvent ev(QEvent::MouseMove, pos, view.mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
    const QStringList lines = view.hoverText();
    // Take the badge back off again. It is drawn into the view, so a reading left
    // behind would show up as a difference in every render compared afterwards.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&view, &leave);
    return lines.isEmpty() ? 0.0 : lines[0].split(' ').first().toDouble();
}

bool isTraceRed(const QColor& c) { return c.red() > 150 && c.green() < 110 && c.blue() < 110; }
bool isSeriesBlue(const QColor& c) { return c.blue() > 140 && c.red() < 110 && c.green() < 130; }

// The last pair a view reported for one of its axes, and how many times it said
// so. QSignalSpy would do this, but it lives in Qt6::Test, which this qtbase is
// deliberately not built with.
struct RangeWatch {
    double lo = 0, hi = 0;
    int calls = 0;

    template <typename View, typename Signal>
    RangeWatch(View* view, Signal sig) {
        QObject::connect(view, sig, view, [this](double l, double h) {
            lo = l;
            hi = h;
            ++calls;
        });
    }
};

// A sounding reaching well above the conventional 100 hPa top, so the automatic
// frame has something to extend to and a pin has something to take back.
analysis::Sounding deepSounding() {
    analysis::Sounding s;
    s.point = {45.0, 10.0};
    for (double p : {10.0, 30.0, 70.0, 100.0, 200.0, 300.0, 500.0, 700.0, 850.0, 1000.0}) {
        analysis::SoundingLevel lvl;
        lvl.pressure = p;
        lvl.tempK = 273.0f - static_cast<float>((1000.0 - p) * 0.02);
        lvl.dewpointK = lvl.tempK - 5.0f;
        s.levels.push_back(lvl);
    }
    return s;
}

analysis::TimeSeries makeSeries() {
    analysis::TimeSeries ts;
    for (int i = 0; i < 12; ++i) {
        ts.times.push_back(core::TimePoint{1'600'000'000 + i * 3600});
        ts.values.push_back(static_cast<float>(280.0 + i));  // 280..291
    }
    return ts;
}

analysis::CrossSection makeSection() {
    const int ns = 40;
    analysis::CrossSection cs;
    cs.units = "K";
    for (int s = 0; s < ns; ++s) {
        cs.points.push_back({60.0, 0.0 + 0.5 * s});
        cs.distancesKm.push_back(28.0 * s);
    }
    for (double p : {200.0, 300.0, 500.0, 700.0, 1000.0}) {
        std::vector<double> press(ns, p);
        std::vector<float> vals;
        for (int s = 0; s < ns; ++s)
            vals.push_back(static_cast<float>(240.0 + 0.05 * (p - 200.0) + 0.1 * s));
        cs.pressures.push_back(std::move(press));
        cs.values.push_back(std::move(vals));
    }
    return cs;
}
}  // namespace

// ------------------------------------------------------------------- AxisRange

TEST(AxisRange, DrawsTheFitUntilItIsPinned) {
    app::AxisRange r;
    EXPECT_TRUE(r.automatic());
    EXPECT_EQ(r.resolve(2.0, 8.0), std::make_pair(2.0, 8.0));

    r.set(3.0, 4.0);
    EXPECT_EQ(r.resolve(2.0, 8.0), std::make_pair(2.0, 8.0)) << "still automatic";

    r.setAutomatic(false);
    EXPECT_EQ(r.resolve(2.0, 8.0), std::make_pair(3.0, 4.0));

    r.setAutomatic(true);  // handing it back restores the fit, with no data reload
    EXPECT_EQ(r.resolve(2.0, 8.0), std::make_pair(2.0, 8.0));
}

// A spin box passes through half-typed states on the way to a value. An axis that
// drew itself upside down on the way there would be worse than one that waits.
TEST(AxisRange, DropsAPairThatIsNotARange) {
    app::AxisRange r;
    r.setAutomatic(false);
    r.set(5.0, 5.0);
    EXPECT_EQ(r.resolve(1.0, 9.0), std::make_pair(1.0, 9.0));
    r.set(9.0, 1.0);
    EXPECT_EQ(r.resolve(1.0, 9.0), std::make_pair(1.0, 9.0));
    r.set(2.0, 3.0);
    EXPECT_EQ(r.resolve(1.0, 9.0), std::make_pair(2.0, 3.0));
}

// Unticking "Auto" before typing anything must not empty the plot.
TEST(AxisRange, ManualWithoutValuesStillDrawsTheFit) {
    app::AxisRange r;
    r.setAutomatic(false);
    EXPECT_EQ(r.resolve(-7.0, 7.0), std::make_pair(-7.0, 7.0));
}

// --------------------------------------------------------------------- Skew-T

// The pin overrides the fit that #19 added: a sounding reaching 10 hPa draws to
// 10 hPa on its own, and back to the conventional frame when asked for one.
TEST(SkewTAxisLimits, PinnedPressureOverridesTheFitToTheData) {
    app::SkewTView view;
    view.resize(500, 560);
    view.setSounding(deepSounding());
    const QImage fitted = renderView(view);

    // Halfway down a log-pressure axis is the geometric mean of its ends. The fit
    // follows the sounding to 10 hPa, so that row reads sqrt(10 x 1050) = 102 hPa.
    const QPointF middle(250.0, 24.0 + 506.0 / 2.0);
    EXPECT_NEAR(pressureUnderCursor(view, middle), 102.0, 2.0);

    // Pinned back to the conventional frame, the same row is sqrt(100 x 1050).
    view.setPressureLimits(100.0, 1050.0);
    view.setPressureAuto(false);
    EXPECT_NEAR(pressureUnderCursor(view, middle), 324.0, 3.0);
    EXPECT_NE(renderView(view), fitted);

    view.setPressureAuto(true);
    EXPECT_NEAR(pressureUnderCursor(view, middle), 102.0, 2.0);
    EXPECT_EQ(renderView(view), fitted) << "handing the axis back did not restore the frame";
}

TEST(SkewTAxisLimits, ReportsTheFrameItDraws) {
    app::SkewTView view;
    view.resize(500, 560);
    RangeWatch press(&view, &app::SkewTView::pressureRangeChanged);
    view.setSounding(deepSounding());
    ASSERT_GT(press.calls, 0);
    EXPECT_NEAR(press.lo, 10.0, 0.01) << "the fit follows the data";

    view.setPressureLimits(200.0, 900.0);
    view.setPressureAuto(false);
    EXPECT_NEAR(press.lo, 200.0, 0.01);
    EXPECT_NEAR(press.hi, 900.0, 0.01);
}

// A pinned temperature window is a window, not a request for room: the diagram
// keeps the conventional skew and clips, rather than de-skewing to fit the data in.
TEST(SkewTAxisLimits, PinnedTemperatureWindowClipsRatherThanReflows) {
    app::SkewTView view;
    view.resize(500, 560);
    view.setSounding(deepSounding());
    const QRect plot(44, 24, 398, 506);
    const int fitted = countWhere(renderView(view), plot, isTraceRed);
    ASSERT_GT(fitted, 100);

    view.setTemperatureLimits(-5.0, 5.0);  // far narrower than the sounding spans
    view.setTemperatureAuto(false);
    EXPECT_LT(countWhere(renderView(view), plot, isTraceRed), fitted);
}

// ---------------------------------------------------------------- Time series

TEST(TimeSeriesAxisLimits, PinnedValueRangeClipsTheTraceToTheBox) {
    app::TimeSeriesView view;
    view.resize(640, 320);
    view.setSeries(makeSeries(), QStringLiteral("t"));
    const QRect plot(64, 22, 640 - 64 - 16, 320 - 22 - 40);
    ASSERT_GT(countWhere(renderView(view), plot, isSeriesBlue), 20);

    // A window the series sits entirely above: nothing of it belongs on the page,
    // and nothing of it may be drawn outside the box either.
    view.setValueLimits(100.0, 200.0);
    view.setValueAuto(false);
    EXPECT_EQ(countWhere(renderView(view), plot, isSeriesBlue), 0);
    EXPECT_EQ(countWhere(renderView(view), QRect(0, 0, 640, 320), isSeriesBlue), 0)
        << "trace escaped the plot box";

    view.setValueAuto(true);
    EXPECT_GT(countWhere(renderView(view), plot, isSeriesBlue), 20);
}

TEST(TimeSeriesAxisLimits, ReportsTheAxisItDraws) {
    app::TimeSeriesView view;
    view.resize(640, 320);
    RangeWatch value(&view, &app::TimeSeriesView::valueRangeChanged);
    view.setSeries(makeSeries(), QStringLiteral("t"));
    ASSERT_GT(value.calls, 0);
    // 280..291 padded by 8% of the span on each side.
    EXPECT_NEAR(value.lo, 280.0 - 0.88, 0.05);
    EXPECT_NEAR(value.hi, 291.0 + 0.88, 0.05);
}

// -------------------------------------------------------------- Cross-section

// Limits move the sampling, not just the labels. The field is re-rendered over the
// narrower span, so the raster must actually change — and the cache must not hand
// back the one built for the old axes.
TEST(CrossSectionAxisLimits, NarrowingAnAxisResamplesTheField) {
    app::CrossSectionView view;
    view.resize(700, 420);
    view.setSection(makeSection());
    const QImage whole = renderView(view);

    view.setDistanceLimits(0.0, 300.0);  // the path runs to 1092 km
    view.setDistanceAuto(false);
    const QImage nearHalf = renderView(view);
    EXPECT_NE(whole, nearHalf) << "distance limit did not reach the raster";

    view.setDistanceAuto(true);
    EXPECT_EQ(renderView(view), whole) << "handing the axis back did not restore the view";

    view.setPressureLimits(300.0, 700.0);
    view.setPressureAuto(false);
    EXPECT_NE(renderView(view), whole) << "pressure limit did not reach the raster";
}

TEST(CrossSectionAxisLimits, ReportsTheAxesItDraws) {
    app::CrossSectionView view;
    view.resize(700, 420);
    RangeWatch press(&view, &app::CrossSectionView::pressureRangeChanged);
    RangeWatch dist(&view, &app::CrossSectionView::distanceRangeChanged);
    view.setSection(makeSection());

    ASSERT_GT(press.calls, 0);
    EXPECT_NEAR(press.lo, 200.0, 0.01);
    EXPECT_NEAR(press.hi, 1000.0, 0.01);
    ASSERT_GT(dist.calls, 0);
    EXPECT_NEAR(dist.lo, 0.0, 0.01);
    EXPECT_NEAR(dist.hi, 28.0 * 39, 0.01);
}
