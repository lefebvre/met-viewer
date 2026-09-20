#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include <QColor>
#include <QImage>
#include <QRect>
#include <Qt>

#include "viewer/analysis/sounding.h"
#include "viewer/app/skewtview.h"

using namespace met;

namespace {
analysis::Sounding makeSounding(bool withWind, bool withHeights = false) {
    analysis::Sounding s;
    s.point = {45.0, 10.0};
    for (double p : {250.0, 400.0, 500.0, 700.0, 850.0, 1000.0}) {
        analysis::SoundingLevel lvl;
        lvl.pressure = p;
        lvl.tempK = 273.0f - static_cast<float>((1000.0 - p) * 0.02);
        lvl.dewpointK = lvl.tempK - 5.0f;
        if (withWind) {
            lvl.windU = 15.0f;  // ~36 kt -> full barbs, clearly visible
            lvl.windV = 10.0f;
        }
        if (withHeights)  // roughly ISA, enough to be a plausible altitude ladder
            lvl.heightGpm = static_cast<float>(44330.0 * (1.0 - std::pow(p / 1013.25, 0.190263)));
        s.levels.push_back(lvl);
    }
    return s;
}

// Temperature (K) of a standard-atmosphere-like profile at `p` hPa, interpolated in
// log-p between anchors. The stratospheric warming above 30 hPa is the part that
// matters here: it is what carries the top of the trace toward the right-hand edge of
// a skewed diagram, and so what the frame has to make room for.
float stdAtmTempK(double p) {
    static const std::vector<std::pair<double, double>> anchors = {
        {1000.0, 15.0}, {500.0, -21.0}, {300.0, -45.0}, {200.0, -56.5}, {100.0, -56.5},
        {50.0, -54.0},  {30.0, -47.0},  {10.0, -35.0},  {3.0, -24.0},   {1.0, -3.0}};
    for (std::size_t i = 0; i + 1 < anchors.size(); ++i) {
        const auto& hi = anchors[i];  // the higher pressure of the pair
        const auto& lo = anchors[i + 1];
        if (p <= hi.first && p >= lo.first) {
            const double f =
                (std::log(p) - std::log(hi.first)) / (std::log(lo.first) - std::log(hi.first));
            return static_cast<float>(273.15 + std::lerp(hi.second, lo.second, f));
        }
    }
    return static_cast<float>(273.15 + anchors.back().second);
}

// A sounding reaching up to `topHpa`, on the pressure levels a GFS or ERA5 file
// carries. Above 100 hPa is the range the conventional skew-T frame cannot show.
analysis::Sounding makeDeepSounding(double topHpa) {
    analysis::Sounding s;
    s.point = {45.0, 10.0};
    for (double p : {1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0, 30.0, 50.0, 70.0, 100.0, 150.0, 200.0,
                     300.0, 500.0, 700.0, 850.0, 1000.0}) {  // top -> bottom, as Sounding wants
        if (p < topHpa) continue;
        analysis::SoundingLevel lvl;
        lvl.pressure = p;
        lvl.tempK = stdAtmTempK(p);
        lvl.dewpointK = lvl.tempK - 5.0f;
        s.levels.push_back(lvl);
    }
    return s;
}

// Count dark pixels in the right-hand wind gutter, above the bottom axis labels.
int gutterInk(const QImage& img) {
    int ink = 0;
    for (int y = 30; y < 520; ++y)
        for (int x = img.width() - 54; x < img.width() - 6; ++x)
            if (qGray(img.pixel(x, y)) < 120) ++ink;
    return ink;
}

// Ink in the strip just inside the left edge, where the altitude ladder is drawn.
int heightStripInk(const QImage& img) {
    int ink = 0;
    for (int y = 30; y < 520; ++y)
        for (int x = 48; x < 110; ++x)
            if (qGray(img.pixel(x, y)) < 200) ++ink;
    return ink;
}

// Pixels of the red temperature trace inside `area`. The trace is the only strongly
// red ink on the diagram: the isotherms behind it are drawn at alpha 120 and wash out
// to pink over the background.
int traceInk(const QImage& img, const QRect& area) {
    int ink = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 110 && c.blue() < 110) ++ink;
        }
    return ink;
}

QImage renderSkewT(app::SkewTView& view, const analysis::Sounding& s) {
    view.setSounding(s);
    QImage img(view.size(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    view.render(&img);
    return img;
}
}  // namespace

TEST(SkewTView, DrawsWindColumnOnlyWhenWindPresent) {
    app::SkewTView view;
    view.resize(500, 560);

    const QImage noWind = renderSkewT(view, makeSounding(false));
    const QImage withWind = renderSkewT(view, makeSounding(true));

    // A wind-free sounding leaves the gutter empty; U/V data fills it with barbs.
    EXPECT_LT(gutterInk(noWind), 10);
    EXPECT_GT(gutterInk(withWind), gutterInk(noWind) + 30);
}

TEST(SkewTView, LabelsHeightsOnlyWhenTheSoundingCarriesThem) {
    app::SkewTView view;
    view.resize(500, 560);

    const QImage noHeights = renderSkewT(view, makeSounding(false));
    const QImage withHeights = renderSkewT(view, makeSounding(false, /*withHeights=*/true));

    // Height data adds an altitude label to every standard isobar in range; with
    // none, the strip holds only the background grid it holds in both.
    EXPECT_GT(heightStripInk(withHeights), heightStripInk(noHeights) + 100);
}

TEST(SkewTView, HeightLabelsToggleOff) {
    app::SkewTView view;
    view.resize(500, 560);
    const analysis::Sounding s = makeSounding(false, /*withHeights=*/true);

    EXPECT_TRUE(view.heightLabelsEnabled());
    const QImage on = renderSkewT(view, s);
    view.setHeightLabelsEnabled(false);
    const QImage off = renderSkewT(view, s);
    view.setHeightLabelsEnabled(true);
    const QImage backOn = renderSkewT(view, s);

    // Toggling off strips the altitude ladder down to the background grid, and
    // toggling back on restores it — the sounding itself never changed.
    EXPECT_GT(heightStripInk(on), heightStripInk(off) + 100);
    EXPECT_EQ(heightStripInk(backOn), heightStripInk(on));
}

TEST(SkewTView, ReportsHeightAvailabilityPerSounding) {
    app::SkewTView view;

    bool available = false;
    QObject::connect(&view, &app::SkewTView::heightsAvailableChanged,
                     [&available](bool a) { available = a; });

    view.setSounding(makeSounding(false, /*withHeights=*/true));
    EXPECT_TRUE(available);
    EXPECT_TRUE(view.hasHeights());

    // A time step whose dataset lacks the height field must gray the toggle out
    // again, not leave it advertising labels that cannot be drawn.
    view.setSounding(makeSounding(false, /*withHeights=*/false));
    EXPECT_FALSE(available);
    EXPECT_FALSE(view.hasHeights());
}

// The diagram used to stop at 100 hPa whatever the data did, so a sounding carrying
// stratospheric levels simply lost them: the levels were in the sounding, and the
// clip in paintEvent dropped them.
TEST(SkewTView, DrawsLevelsAboveTheConventionalHundredHectopascalTop) {
    app::SkewTView view;
    view.resize(500, 560);

    // The top fifth of the plot rect (margins 44/58/24/30), which a 100 hPa axis can
    // put no data in. Its left corner holds the legend, whose temperature swatch is
    // the same red as the trace, so the strip starts clear of it.
    const QRect upper(184, 24, 442 - 184, 100);

    const QImage shallow = renderSkewT(view, makeSounding(false));
    EXPECT_EQ(traceInk(shallow, upper), 0);

    const QImage deep = renderSkewT(view, makeDeepSounding(1.0));
    EXPECT_GT(traceInk(deep, upper), 20);
}

// Making room upward is only half of it: the skew shifts the top of the frame right by
// most of the plot width, so an extended axis will push a warm stratopause off the
// right-hand edge unless the frame relaxes. Trace ink piled against that edge is what
// the clipping looks like.
TEST(SkewTView, ExtendedFrameHoldsTheWholeTraceInsideThePlot) {
    app::SkewTView view;
    view.resize(500, 560);

    const QImage deep = renderSkewT(view, makeDeepSounding(1.0));
    ASSERT_GT(traceInk(deep, QRect(44, 24, 398, 506)), 200) << "no trace drawn at all";
    EXPECT_EQ(traceInk(deep, QRect(439, 24, 3, 506)), 0);
}
