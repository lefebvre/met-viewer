#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <QColor>
#include <QImage>
#include <Qt>

#include "viewer/analysis/crosssection.h"
#include "viewer/app/crosssectionview.h"

using namespace met;

namespace {
// A temperature section over five isobaric levels, optionally carrying the
// geopotential height of each level. Heights tilt along the path so the isopleths
// slope, as they do over a real thermal gradient.
analysis::CrossSection makeSection(bool withHeights) {
    const int ns = 40;
    analysis::CrossSection cs;
    cs.units = "K";
    for (int s = 0; s < ns; ++s) {
        cs.points.push_back({60.0, 0.0 + 0.5 * s});
        cs.distancesKm.push_back(28.0 * s);
    }
    for (double p : {200.0, 300.0, 500.0, 700.0, 1000.0}) {
        const double isa = 44330.0 * (1.0 - std::pow(p / 1013.25, 0.190263));
        std::vector<double> press(ns, p), height;
        std::vector<float> vals;
        for (int s = 0; s < ns; ++s) {
            vals.push_back(static_cast<float>(240.0 + 0.05 * (p - 200.0) + 0.1 * s));
            height.push_back(isa * (1.0 + 0.004 * s));
        }
        cs.pressures.push_back(std::move(press));
        cs.values.push_back(std::move(vals));
        if (withHeights) cs.heights.push_back(std::move(height));
    }
    return cs;
}

// Pixels inside the plot that are near-neutral and bright: the pale halo under the
// isopleths and the backing of their labels. The colormap underneath is turbo,
// which has no grey anywhere in it.
int isoplethInk(const QImage& img) {
    int ink = 0;
    for (int y = 20; y < 360; ++y) {
        for (int x = 60; x < 580; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - c.green()) < 20 && std::abs(c.green() - c.blue()) < 20 &&
                qGray(c.rgb()) > 200)
                ++ink;
        }
    }
    return ink;
}

QImage renderSection(app::CrossSectionView& view) {
    QImage img(view.size(), QImage::Format_ARGB32);
    img.fill(Qt::white);
    view.render(&img);
    return img;
}
}  // namespace

TEST(CrossSectionView, DrawsHeightIsoplethsWhenTheSectionCarriesHeights) {
    app::CrossSectionView view;
    view.resize(600, 400);

    view.setSection(makeSection(/*withHeights=*/false));
    EXPECT_FALSE(view.hasHeights());
    const int plain = isoplethInk(renderSection(view));

    view.setSection(makeSection(/*withHeights=*/true));
    EXPECT_TRUE(view.hasHeights());
    const int contoured = isoplethInk(renderSection(view));

    EXPECT_GT(contoured, plain + 200);
}

TEST(CrossSectionView, HeightIsoplethsFollowTheToggle) {
    app::CrossSectionView view;
    view.resize(600, 400);
    view.setSection(makeSection(/*withHeights=*/true));

    const int on = isoplethInk(renderSection(view));
    view.setHeightContoursEnabled(false);
    const int off = isoplethInk(renderSection(view));
    EXPECT_FALSE(view.heightContoursEnabled());
    EXPECT_GT(on, off + 200);

    view.setHeightContoursEnabled(true);
    EXPECT_GT(isoplethInk(renderSection(view)), off + 200);
}
