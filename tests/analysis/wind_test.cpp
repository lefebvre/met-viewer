#include <gtest/gtest.h>

#include <cmath>

#include "viewer/analysis/wind.h"
#include "viewer/core/grid.h"
#include "viewer/render/windbarb.h"

using namespace met;

TEST(Wind, FindsPairs) {
    auto p = analysis::findWindPair({"t", "u", "v", "gh"});
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->uName, "u");
    EXPECT_EQ(p->vName, "v");

    auto p2 = analysis::findWindPair({"10u", "10v", "t2m"});
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(p2->uName, "10u");

    EXPECT_FALSE(analysis::findWindPair({"t", "gh"}).has_value());
}

TEST(Wind, BarbQuantization) {
    // 0 kt -> nothing (calm).
    auto c0 = render::quantizeBarb(2.0);
    EXPECT_EQ(c0.pennants + c0.full + c0.half, 0);
    // 5 kt -> one half barb.
    auto c5 = render::quantizeBarb(5.0);
    EXPECT_EQ(c5.half, 1);
    EXPECT_EQ(c5.full, 0);
    // 25 kt -> two full + one half.
    auto c25 = render::quantizeBarb(25.0);
    EXPECT_EQ(c25.full, 2);
    EXPECT_EQ(c25.half, 1);
    // 75 kt -> one pennant + two full + one half.
    auto c75 = render::quantizeBarb(75.0);
    EXPECT_EQ(c75.pennants, 1);
    EXPECT_EQ(c75.full, 2);
    EXPECT_EQ(c75.half, 1);
    // Rounds to nearest 5.
    EXPECT_EQ(render::quantizeBarb(12.0).full, 1);  // 12 -> 10
    EXPECT_EQ(render::quantizeBarb(13.0).half, 1);  // 13 -> 15 -> 1 full + 1 half
}

namespace {
met::analysis::WindField makeUniformWind(float u, float v) {
    core::RegularLatLonGrid g;
    g.lat0 = 40; g.lon0 = 0; g.dlat = -1; g.dlon = 1; g.nlon = 4; g.nlat = 4;
    met::analysis::WindField w;
    w.u.grid = g; w.v.grid = g;
    w.u.values.assign(16, u);
    w.v.values.assign(16, v);
    return w;
}
}  // namespace

TEST(Wind, SpeedAndSample) {
    auto w = makeUniformWind(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(analysis::windSpeed(w, 1.0, 1.0), 5.0f);  // 3-4-5
    const analysis::UV uv = analysis::sampleWind(w, 1.5, 2.0);
    EXPECT_FLOAT_EQ(uv.u, 3.0f);
    EXPECT_FLOAT_EQ(uv.v, 4.0f);
}

TEST(Wind, RegularGridNoRotation) {
    auto w = makeUniformWind(10.0f, 0.0f);
    w.u.meta.gridRelativeWind = true;  // even if flagged, lat/lon never rotates
    analysis::rotateToEarthRelative(w);
    EXPECT_FLOAT_EQ(w.u.values[0], 10.0f);
    EXPECT_FLOAT_EQ(w.v.values[0], 0.0f);
}

TEST(Wind, ProjectedGridRotationAngleIsSmallNearCentralMeridian) {
    // Lambert with central meridian at lon_0 = 260 (== -100). On the central
    // meridian the grid north aligns with true north, so the angle is ~0; off
    // the meridian it is non-zero.
    core::ProjectedGrid pg;
    pg.crs = core::Crs("+proj=lcc +lat_1=40 +lat_2=40 +lat_0=40 +lon_0=260 +R=6371229 +units=m +no_defs");
    pg.nx = 20; pg.ny = 12; pg.dx = 50000; pg.dy = 50000;
    (void)pg.crs.forward(260.0, 30.0, pg.x0, pg.y0);  // anchor on the meridian
    core::GridDef g = pg;

    const double angCenter = analysis::gridNorthAngle(g, 0, 5);   // near lon_0
    const double angEast = analysis::gridNorthAngle(g, 19, 5);    // well east
    EXPECT_NEAR(angCenter, 0.0, 0.02);
    EXPECT_GT(std::abs(angEast), 0.05);
}
