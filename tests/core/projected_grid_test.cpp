#include <gtest/gtest.h>

#include <cmath>

#include "viewer/core/crs.h"
#include "viewer/core/grid.h"

using namespace met::core;

namespace {
// A Lambert conformal conic grid over the central US (spherical earth).
GridDef lambertGrid() {
    ProjectedGrid g;
    g.crs = Crs("+proj=lcc +lat_1=40 +lat_2=40 +lat_0=40 +lon_0=260 +R=6371229 +units=m +no_defs");
    g.nx = 16;
    g.ny = 12;
    g.dx = 50000.0;
    g.dy = 50000.0;
    // Anchor grid point (0,0) at the projection of (lat 30, lon 250).
    double x0 = 0, y0 = 0;
    (void)g.crs.forward(250.0, 30.0, x0, y0);
    g.x0 = x0;
    g.y0 = y0;
    return g;
}
}  // namespace

TEST(Crs, LambertRoundTrip) {
    Crs crs("+proj=lcc +lat_1=40 +lat_2=40 +lat_0=40 +lon_0=260 +R=6371229 +units=m +no_defs");
    double x = 0, y = 0, lon = 0, lat = 0;
    ASSERT_TRUE(crs.forward(-100.0, 42.0, x, y));
    ASSERT_TRUE(crs.inverse(x, y, lon, lat));
    EXPECT_NEAR(lon, -100.0, 1e-6);
    EXPECT_NEAR(lat, 42.0, 1e-6);
}

TEST(ProjectedGrid, DimensionsAndCount) {
    const GridDef g = lambertGrid();
    EXPECT_EQ(gridWidth(g), 16);
    EXPECT_EQ(gridHeight(g), 12);
    EXPECT_EQ(gridCount(g), 192u);
}

TEST(ProjectedGrid, FirstPointMapsToAnchor) {
    const GridDef g = lambertGrid();
    const LatLon ll = indexToLatLon(g, 0, 0);
    // 250 E == -110 W after wrapping.
    EXPECT_NEAR(ll.lat, 30.0, 1e-4);
    EXPECT_NEAR(ll.lon, -110.0, 1e-4);
}

TEST(ProjectedGrid, IndexRoundTrip) {
    const GridDef g = lambertGrid();
    for (int j = 0; j < gridHeight(g); ++j) {
        for (int i = 0; i < gridWidth(g); ++i) {
            const LatLon ll = indexToLatLon(g, i, j);
            const GridIndex gi = latlonToIndex(g, ll);
            ASSERT_TRUE(gi.inDomain) << i << "," << j;
            EXPECT_NEAR(gi.x, i, 1e-4);
            EXPECT_NEAR(gi.y, j, 1e-4);
        }
    }
}

TEST(ProjectedGrid, OutOfDomainFlagged) {
    const GridDef g = lambertGrid();
    // A point on the far side of the planet is outside the grid.
    const GridIndex gi = latlonToIndex(g, LatLon{-40.0, 100.0});
    EXPECT_FALSE(gi.inDomain);
}

TEST(ProjectedGrid, BBoxCoversGrid) {
    const BBox b = gridBBox(lambertGrid());
    // The grid is a small patch over the central US.
    EXPECT_GT(b.minLat, 25.0);
    EXPECT_LT(b.maxLat, 45.0);
    EXPECT_GT(b.minLon, -120.0);
    EXPECT_LT(b.maxLon, -95.0);
}
