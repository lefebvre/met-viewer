#include <gtest/gtest.h>

#include <cmath>

#include <QColor>
#include <QImage>
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
