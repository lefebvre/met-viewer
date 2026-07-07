#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <Qt>

#include "viewer/analysis/sounding.h"
#include "viewer/app/skewtview.h"

using namespace met;

namespace {
analysis::Sounding makeSounding(bool withWind) {
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
