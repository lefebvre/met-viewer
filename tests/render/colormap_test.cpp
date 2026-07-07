#include <gtest/gtest.h>

#include <cmath>

#include "viewer/render/colormap.h"

using namespace met::render;

TEST(Colormap, EndpointsMatchViridis) {
    Colormap cm = Colormap::builtin("viridis");
    cm.setRange(0.0, 1.0);
    // Viridis starts dark purple (68,1,84) and ends yellow (~253,231,37).
    const Rgba lo = cm.map(0.0);
    EXPECT_EQ(lo.r, 68);
    EXPECT_EQ(lo.g, 1);
    EXPECT_EQ(lo.b, 84);
    const Rgba hi = cm.map(1.0);
    EXPECT_GT(hi.r, 240);
    EXPECT_GT(hi.g, 220);
    EXPECT_LT(hi.b, 60);
}

TEST(Colormap, ClampsOutsideRange) {
    Colormap cm = Colormap::builtin("viridis");
    cm.setRange(10.0, 20.0);
    EXPECT_EQ(cm.map(-5.0).r, cm.map(10.0).r);
    EXPECT_EQ(cm.map(999.0).r, cm.map(20.0).r);
}

TEST(Colormap, NanIsTransparent) {
    Colormap cm = Colormap::builtin("turbo");
    cm.setRange(0.0, 1.0);
    const Rgba c = cm.map(std::nan(""));
    EXPECT_EQ(c.a, 0);
}

TEST(Colormap, MidpointIsInterior) {
    Colormap cm = Colormap::builtin("viridis");
    cm.setRange(0.0, 1.0);
    const Rgba mid = cm.map(0.5);
    // Viridis midpoint is a teal-green, distinct from both ends.
    EXPECT_GT(mid.g, 120);
    EXPECT_LT(mid.r, 120);
}
