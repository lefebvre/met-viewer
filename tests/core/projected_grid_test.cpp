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

// A polar-stereographic grid centered on the north pole (the pole projects to
// the grid center), so gridBBox must detect the enclosed pole.
GridDef polarGrid() {
    ProjectedGrid g;
    g.crs = Crs("+proj=stere +lat_0=90 +lat_ts=60 +lon_0=0 +R=6371229 +units=m +no_defs");
    g.nx = 11;
    g.ny = 11;
    g.dx = 200000.0;
    g.dy = 200000.0;
    g.x0 = -g.dx * (g.nx - 1) / 2.0;  // center index (5,5) at projected (0,0) = pole
    g.y0 = -g.dy * (g.ny - 1) / 2.0;
    return g;
}

// A Mercator grid centered on lon 180, straddling the ±180° antimeridian.
GridDef datelineGrid() {
    ProjectedGrid g;
    g.crs = Crs("+proj=merc +lon_0=180 +R=6371229 +units=m +no_defs");
    g.nx = 11;
    g.ny = 5;
    g.dx = 111195.0 * 2.0;  // ~2 deg of longitude per cell at the equator
    g.dy = 111195.0 * 2.0;
    g.x0 = -5.0 * g.dx;  // center column at x=0 (lon 180)
    g.y0 = -2.0 * g.dy;  // center row at the equator
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

TEST(ProjectedGrid, BBoxDetectsEnclosedPole) {
    const BBox b = gridBBox(polarGrid());
    // The north pole is inside the grid, so latitude must reach 90 even though
    // the sampled border never touches it, and longitude is unconstrained.
    EXPECT_DOUBLE_EQ(b.maxLat, 90.0);
    EXPECT_DOUBLE_EQ(b.minLon, -180.0);
    EXPECT_DOUBLE_EQ(b.maxLon, 180.0);
    EXPECT_LT(b.minLat, 90.0);  // the grid still extends away from the pole
}

TEST(ProjectedGrid, BBoxSpansAntimeridian) {
    const BBox b = gridBBox(datelineGrid());
    // A grid straddling ±180° must get a narrow covering arc (~20°) centered on
    // 180, expressed with maxLon > 180 — not a spurious near-global span.
    const double span = b.maxLon - b.minLon;
    EXPECT_GT(span, 15.0);
    EXPECT_LT(span, 40.0);
    EXPECT_GT(b.maxLon, 180.0);
    EXPECT_NEAR(0.5 * (b.minLon + b.maxLon), 180.0, 1.0);
}
