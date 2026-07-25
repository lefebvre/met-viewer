#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

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
    g.lat0 = 40;
    g.lon0 = 0;
    g.dlat = -1;
    g.dlon = 1;
    g.nlon = 4;
    g.nlat = 4;
    met::analysis::WindField w;
    w.u.grid = g;
    w.v.grid = g;
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
    pg.crs = core::Crs(
        "+proj=lcc +lat_1=40 +lat_2=40 +lat_0=40 +lon_0=260 +R=6371229 +units=m +no_defs");
    pg.nx = 20;
    pg.ny = 12;
    pg.dx = 50000;
    pg.dy = 50000;
    (void)pg.crs.forward(260.0, 30.0, pg.x0, pg.y0);  // anchor on the meridian
    core::GridDef g = pg;

    const double angCenter = analysis::gridNorthAngle(g, 0, 5);  // near lon_0
    const double angEast = analysis::gridNorthAngle(g, 19, 5);   // well east
    EXPECT_NEAR(angCenter, 0.0, 0.02);
    EXPECT_GT(std::abs(angEast), 0.05);
}

namespace {
constexpr int kLccNx = 20, kLccNy = 12;
constexpr double kLccStep = 50000.0;

// Two views of one Lambert rectangle: the same ground, scanned south-to-north
// (dy > 0, GRIB jScansPositively = 1) or north-to-south (dy < 0). Both are built
// from the same projected corner so index (i, j) in one names the same point as
// (i, ny-1-j) in the other.
core::ProjectedGrid lambertGrid(bool northUp) {
    core::ProjectedGrid pg;
    pg.crs = core::Crs(
        "+proj=lcc +lat_1=40 +lat_2=40 +lat_0=40 +lon_0=260 +R=6371229 +units=m +no_defs");
    pg.nx = kLccNx;
    pg.ny = kLccNy;
    pg.dx = kLccStep;
    pg.dy = northUp ? kLccStep : -kLccStep;
    double sx = 0, sy = 0;
    (void)pg.crs.forward(240.0, 30.0, sx, sy);  // south-west corner of the rectangle
    pg.x0 = sx;
    pg.y0 = northUp ? sy : sy + kLccStep * (kLccNy - 1);  // first scanned row
    return pg;
}
}  // namespace

// The rotation angle describes the projection, not the order rows happen to be
// stored in. GRIB resolves grid-relative u/v against the projection's +x/+y axes
// regardless of scanning mode, so a south-scanning message (dy < 0) must produce
// the same angle at the same place as a north-scanning one. Deriving the angle
// from increasing row index instead flips it by 180 degrees, which silently
// reverses every wind vector on such a file.
TEST(Wind, RotationAngleIsIndependentOfScanDirection) {
    const core::GridDef up = lambertGrid(true);
    const core::GridDef down = lambertGrid(false);

    // Same ground position, opposite row ordering: row j from the south edge is
    // row (ny-1-j) when the grid scans the other way.
    for (int j : {0, 4, 11}) {
        for (int i : {0, 9, 19}) {
            const core::LatLon a = core::indexToLatLon(up, i, j);
            const core::LatLon b = core::indexToLatLon(down, i, 11 - j);
            ASSERT_NEAR(a.lat, b.lat, 1e-6) << "grids must cover the same ground";
            ASSERT_NEAR(a.lon, b.lon, 1e-6);
            EXPECT_NEAR(analysis::gridNorthAngle(up, i, j),
                        analysis::gridNorthAngle(down, i, 11 - j), 1e-6)
                << "at i=" << i << " j=" << j;
        }
    }
}

TEST(Wind, RotationOnSouthScanningGridKeepsWindDirection) {
    // A pure +x (grid-east) wind rotates to nearly-east on both grids. If the
    // angle were 180 degrees out, the south-scanning one would come back westward.
    for (bool northUp : {true, false}) {
        analysis::WindField w;
        const core::GridDef g = lambertGrid(northUp);
        w.u.grid = g;
        w.v.grid = g;
        w.u.values.assign(20u * 12u, 10.0f);
        w.v.values.assign(20u * 12u, 0.0f);
        w.u.meta.gridRelativeWind = true;
        analysis::rotateToEarthRelative(w);
        // Well west of the central meridian the convergence is a modest angle, so
        // the eastward component must stay clearly positive.
        EXPECT_GT(w.u.values[0], 5.0f) << (northUp ? "north-scanning" : "south-scanning");
    }
}

// The lattice-and-interpolate path must agree with evaluating the angle at every
// cell — that is the only thing making it a legitimate optimization rather than a
// different answer.
TEST(Wind, LatticeRotationMatchesPerCellEvaluation) {
    const core::GridDef g = lambertGrid(true);
    const int nx = core::gridWidth(g), ny = core::gridHeight(g);

    analysis::WindField fast;
    fast.u.grid = g;
    fast.v.grid = g;
    const std::size_t n = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    fast.u.values.assign(n, 7.0f);
    fast.v.values.assign(n, -3.0f);
    fast.u.meta.gridRelativeWind = true;
    analysis::rotateToEarthRelative(fast);

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const double t = analysis::gridNorthAngle(g, i, j);
            const double c = std::cos(t), s = std::sin(t);
            const std::size_t k = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                                  static_cast<std::size_t>(i);
            // ~0.01 on a magnitude-7.6 vector is under 0.1 degrees of direction —
            // two orders of magnitude finer than a wind barb's 5-knot quantization,
            // and this 20x12 grid is the worst case (one lattice interval spans it).
            EXPECT_NEAR(fast.u.values[k], 7.0 * c + (-3.0) * s, 0.01);
            EXPECT_NEAR(fast.v.values[k], -7.0 * s + (-3.0) * c, 0.01);
        }
    }
}

// Rotating in place changes the values, so views caching a derived product
// against Field2D::id must be told to rebuild.
TEST(Wind, RotationAssignsFreshFieldIds) {
    analysis::WindField w;
    const core::GridDef g = lambertGrid(true);
    w.u.grid = g;
    w.v.grid = g;
    w.u.values.assign(20u * 12u, 4.0f);
    w.v.values.assign(20u * 12u, 4.0f);
    w.u.meta.gridRelativeWind = true;
    const std::uint64_t beforeU = w.u.id, beforeV = w.v.id;
    analysis::rotateToEarthRelative(w);
    EXPECT_NE(w.u.id, beforeU);
    EXPECT_NE(w.v.id, beforeV);
}
