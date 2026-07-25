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

// A global grid's last cell wraps back onto column 0. latlonToIndex reports such
// a point as in-domain (x runs up to nlon), so rejecting x > nlon-1 here punched a
// dlon-wide NaN stripe through the field — visible as "(no data)" under the cursor
// on ERA5 just west of the prime meridian, while the map drew data there.
TEST(Sample, GlobalWrapSeamIsInterpolatedNotDropped) {
    RegularLatLonGrid g;
    g.lat0 = 90;
    g.lon0 = 0;
    g.dlat = -1;
    g.dlon = 1;
    g.nlon = 360;
    g.nlat = 181;
    g.globalWrapLon = true;
    Field2D f;
    f.grid = g;
    f.values.assign(360u * 181u, 42.0f);

    for (double lon : {0.0, 90.0, 359.0, 359.5, 359.9, -0.5, -0.2}) {
        const float v = sampleBilinear(f, LatLon{45.0, lon});
        EXPECT_FLOAT_EQ(v, 42.0f) << "at lon " << lon;
    }
}

TEST(Sample, GlobalWrapSeamBlendsTheTwoEdgeColumns) {
    RegularLatLonGrid g;
    g.lat0 = 0;
    g.lon0 = 0;
    g.dlat = -1;
    g.dlon = 90;
    g.nlon = 4;
    g.nlat = 2;
    g.globalWrapLon = true;
    Field2D f;
    f.grid = g;
    // Columns 0..3 hold 0, 10, 20, 30; halfway past the last column is (30+0)/2.
    f.values = {0, 10, 20, 30, 0, 10, 20, 30};
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 3.5, 0.0), 15.0f);
    EXPECT_FLOAT_EQ(sampleBilinearIndex(f, 4.0, 0.0), 0.0f);    // full wrap to column 0
    EXPECT_TRUE(std::isnan(sampleBilinearIndex(f, 4.1, 0.0)));  // past the wrap
}

TEST(Sample, BoundedGridStillRejectsPastTheLastColumn) {
    Field2D f = makeField();  // globalWrapLon is false
    EXPECT_FALSE(std::isnan(sampleBilinearIndex(f, 2.0, 0.0)));
    EXPECT_TRUE(std::isnan(sampleBilinearIndex(f, 2.5, 0.0)));
}
