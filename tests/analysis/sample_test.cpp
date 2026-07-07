#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "viewer/analysis/sample.h"
#include "viewer/core/field.h"

using namespace met::core;
using met::analysis::sampleBilinear;
using met::analysis::sampleBilinearIndex;

namespace {
// 3x3 grid, first point (lat 2, lon 0), 1-degree spacing, scanning south & east.
Field2D makeField() {
    RegularLatLonGrid g;
    g.lat0 = 2.0;
    g.lon0 = 0.0;
    g.dlat = -1.0;
    g.dlon = 1.0;
    g.nlon = 3;
    g.nlat = 3;
    Field2D f;
    f.grid = g;
    // value = 10*row + col
    f.values = {0, 1, 2, 10, 11, 12, 20, 21, 22};
    return f;
}
}  // namespace

TEST(Sample, ExactAtGridPoints) {
    Field2D f = makeField();
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 2, 2), 22.0f);
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 1, 1), 11.0f);
}

TEST(Sample, InterpolatesMidpoint) {
    Field2D f = makeField();
    // Between (0,0)=0 and (1,0)=1 at x=0.5 -> 0.5
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 0.5, 0.0), 0.5f);
    // Center of the four top-left cells -> mean(0,1,10,11) = 5.5
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 0.5, 0.5), 5.5f);
}

TEST(Sample, OffGridReturnsNaN) {
    Field2D f = makeField();
    EXPECT_TRUE(std::isnan(sampleBilinearIndex(f, -0.1, 0.0)));
    EXPECT_TRUE(std::isnan(sampleBilinearIndex(f, 0.0, 2.5)));
}

TEST(Sample, ByLatLon) {
    Field2D f = makeField();
    // lat 2, lon 0 is grid point (0,0) = 0.
    EXPECT_FLOAT_EQ(sampleBilinear(f, LatLon{2.0, 0.0}), 0.0f);
    // lat 0, lon 2 is grid point (2,2) = 22.
    EXPECT_FLOAT_EQ(sampleBilinear(f, LatLon{0.0, 2.0}), 22.0f);
    // Outside the grid.
    EXPECT_TRUE(std::isnan(sampleBilinear(f, LatLon{-5.0, 0.0})));
}

TEST(Sample, NanAwareFallback) {
    Field2D f = makeField();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    f.values[0] = nan;  // (0,0) missing
    // Very close to the missing corner falls back to nearest valid value,
    // never returns NaN when other corners are present.
    const float v = sampleBilinearIndex(f, 0.01, 0.01);
    EXPECT_FALSE(std::isnan(v));
}
