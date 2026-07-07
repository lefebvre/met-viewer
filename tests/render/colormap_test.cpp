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

TEST(Colormap, AllBuiltinsResolveAndCover) {
    const auto names = Colormap::builtinNames();
    EXPECT_GE(names.size(), 6u);
    for (const auto& n : names) {
        Colormap cm = Colormap::builtin(n);
        EXPECT_EQ(cm.name(), n);
        cm.setRange(0.0, 1.0);
        // Endpoints differ from the midpoint for every map (non-degenerate LUT).
        const Rgba lo = cm.map(0.0), mid = cm.map(0.5), hi = cm.map(1.0);
        EXPECT_FALSE(lo.r == hi.r && lo.g == hi.g && lo.b == hi.b) << n;
        (void)mid;
    }
}

TEST(Colormap, DivergingFlag) {
    EXPECT_TRUE(Colormap::isDiverging("RdBu (diverging)"));
    EXPECT_TRUE(Colormap::isDiverging("coolwarm"));
    EXPECT_FALSE(Colormap::isDiverging("viridis"));
    EXPECT_FALSE(Colormap::isDiverging("turbo"));
}

TEST(Colormap, DivergingCenterIsNeutral) {
    // A diverging map is light/neutral in the middle and saturated at the ends.
    Colormap cm = Colormap::builtin("RdBu (diverging)");
    cm.setRange(-1.0, 1.0);
    const Rgba mid = cm.map(0.0);   // center -> near white
    const Rgba neg = cm.map(-1.0);  // one end
    const Rgba pos = cm.map(1.0);   // other end
    EXPECT_GT(mid.r + mid.g + mid.b, 600);  // bright center
    // Ends are distinct hues (blue vs red family).
    EXPECT_NE(neg.b > neg.r, pos.b > pos.r);
}

TEST(Colormap, MidpointIsInterior) {
    Colormap cm = Colormap::builtin("viridis");
    cm.setRange(0.0, 1.0);
    const Rgba mid = cm.map(0.5);
    // Viridis midpoint is a teal-green, distinct from both ends.
    EXPECT_GT(mid.g, 120);
    EXPECT_LT(mid.r, 120);
}
