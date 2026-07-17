#include <gtest/gtest.h>

#include <cmath>

#include "viewer/core/grid.h"

using namespace met::core;

namespace {
// A north-to-south, west-to-east grid like our GRIB fixture: 16x8, first point
// (lat 70, lon 0), 2-degree spacing.
GridDef fixtureGrid() {
    RegularLatLonGrid g;
    g.lat0 = 70.0;
    g.lon0 = 0.0;
    g.dlat = -2.0;  // scanning south
    g.dlon = 2.0;   // scanning east
    g.nlon = 16;
    g.nlat = 8;
    g.globalWrapLon = false;
    return g;
}
}  // namespace

TEST(Grid, DimensionsAndCount) {
    const GridDef g = fixtureGrid();
    EXPECT_EQ(gridWidth(g), 16);
    EXPECT_EQ(gridHeight(g), 8);
    EXPECT_EQ(gridCount(g), 128u);
}

TEST(Grid, IndexToLatLonCorners) {
    const GridDef g = fixtureGrid();
    const LatLon nw = indexToLatLon(g, 0, 0);
    EXPECT_DOUBLE_EQ(nw.lat, 70.0);
    EXPECT_DOUBLE_EQ(nw.lon, 0.0);

    const LatLon se = indexToLatLon(g, 15, 7);
    EXPECT_DOUBLE_EQ(se.lat, 56.0);
    EXPECT_DOUBLE_EQ(se.lon, 30.0);
}

TEST(Grid, LatLonToIndexRoundTrip) {
    const GridDef g = fixtureGrid();
    for (int r = 0; r < gridHeight(g); ++r) {
        for (int c = 0; c < gridWidth(g); ++c) {
            const LatLon ll = indexToLatLon(g, c, r);
            const GridIndex gi = latlonToIndex(g, ll);
            ASSERT_TRUE(gi.inDomain) << "cell " << c << "," << r;
            EXPECT_NEAR(gi.x, c, 1e-9);
            EXPECT_NEAR(gi.y, r, 1e-9);
        }
    }
}

TEST(Grid, OutOfDomainFlagged) {
    const GridDef g = fixtureGrid();
    // South of the grid (lat 40 < 56).
    const GridIndex below = latlonToIndex(g, LatLon{40.0, 10.0});
    EXPECT_FALSE(below.inDomain);
    // East of the grid (lon 40 > 30).
    const GridIndex east = latlonToIndex(g, LatLon{60.0, 40.0});
    EXPECT_FALSE(east.inDomain);
}

TEST(Grid, LongitudeWrapConvention) {
    const GridDef g = fixtureGrid();
    // lon -350 is equivalent to +10 and should land at column 5.
    const GridIndex gi = latlonToIndex(g, LatLon{60.0, -350.0});
    EXPECT_TRUE(gi.inDomain);
    EXPECT_NEAR(gi.x, 5.0, 1e-9);
}

TEST(Grid, BBox) {
    const BBox b = gridBBox(fixtureGrid());
    EXPECT_DOUBLE_EQ(b.minLat, 56.0);
    EXPECT_DOUBLE_EQ(b.maxLat, 70.0);
    EXPECT_DOUBLE_EQ(b.minLon, 0.0);
    EXPECT_DOUBLE_EQ(b.maxLon, 30.0);
}

TEST(Grid, SameGridReflexiveAndNoiseTolerant) {
    EXPECT_TRUE(sameGrid(fixtureGrid(), fixtureGrid()));
    // Sub-ppm representational noise still compares equal (mergeable slices).
    auto g = std::get<RegularLatLonGrid>(fixtureGrid());
    g.lat0 += 1e-9;
    g.dlon += 1e-9;
    EXPECT_TRUE(sameGrid(fixtureGrid(), GridDef{g}));
}

TEST(Grid, SameGridRejectsDifferentGeometry) {
    // Different dimensions, origin, or spacing => not the same grid.
    auto dims = std::get<RegularLatLonGrid>(fixtureGrid());
    dims.nlon = 20;
    EXPECT_FALSE(sameGrid(fixtureGrid(), GridDef{dims}));
    auto origin = std::get<RegularLatLonGrid>(fixtureGrid());
    origin.lon0 = 10.0;
    EXPECT_FALSE(sameGrid(fixtureGrid(), GridDef{origin}));
    auto spacing = std::get<RegularLatLonGrid>(fixtureGrid());
    spacing.dlat = -3.0;
    EXPECT_FALSE(sameGrid(fixtureGrid(), GridDef{spacing}));
}

TEST(Grid, SameGridDistinguishesAlternativesAndProjections) {
    ProjectedGrid p;
    p.crs = Crs("+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=-97.5 +R=6371200 +units=m");
    p.x0 = -2.7e6;
    p.y0 = -1.6e6;
    p.dx = p.dy = 3000.0;
    p.nx = 1799;
    p.ny = 1059;
    // A lat/lon grid and a projected grid are never the same.
    EXPECT_FALSE(sameGrid(fixtureGrid(), GridDef{p}));
    EXPECT_TRUE(sameGrid(GridDef{p}, GridDef{p}));
    // Same geometry but a different central meridian (the ARL bug) => different grid.
    ProjectedGrid q = p;
    q.crs = Crs("+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=0 +R=6371200 +units=m");
    EXPECT_FALSE(sameGrid(GridDef{p}, GridDef{q}));
}
