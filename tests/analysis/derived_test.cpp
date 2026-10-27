#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "viewer/analysis/derived.h"
#include "viewer/analysis/wind.h"
#include "viewer/core/grid.h"

using namespace met;

namespace {
// A projected grid in metres so finite differences are exact (no cos-lat metric).
core::ProjectedGrid metreGrid(int n, double spacing) {
    core::ProjectedGrid g;
    g.crs = core::Crs("+proj=stere +lat_0=90 +lat_ts=60 +lon_0=0 +R=6371229 +units=m +no_defs");
    g.nx = n;
    g.ny = n;
    g.dx = spacing;
    g.dy = spacing;
    g.x0 = 0;
    g.y0 = 0;
    return g;
}

// Build a wind field from analytic u(i,j), v(i,j) on a metre grid. The
// components are along the grid's x/y axes, and flagged so.
template <typename FU, typename FV>
analysis::WindField makeWind(int n, double spacing, FU fu, FV fv) {
    analysis::WindField w;
    core::ProjectedGrid g = metreGrid(n, spacing);
    w.u.grid = g;
    w.v.grid = g;
    w.u.meta.gridRelativeWind = true;
    w.v.meta.gridRelativeWind = true;
    const std::size_t nn = static_cast<std::size_t>(n);
    w.u.values.resize(nn * nn);
    w.v.values.resize(nn * nn);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = i * spacing, y = j * spacing;
            const std::size_t k = static_cast<std::size_t>(j) * nn + static_cast<std::size_t>(i);
            w.u.values[k] = static_cast<float>(fu(x, y));
            w.v.values[k] = static_cast<float>(fv(x, y));
        }
    return w;
}
}  // namespace

TEST(Derived, WindSpeed) {
    auto w =
        makeWind(5, 1000, [](double, double) { return 3.0; }, [](double, double) { return 4.0; });
    const auto sp = analysis::windSpeedField(w);
    EXPECT_EQ(sp.meta.varName, "wspd");
    for (float v : sp.values) EXPECT_FLOAT_EQ(v, 5.0f);  // 3-4-5
}

TEST(Derived, WindDirection) {
    // Wind blowing toward the north (v>0) comes FROM the south -> 180 deg.
    auto north =
        makeWind(3, 1000, [](double, double) { return 0.0; }, [](double, double) { return 5.0; });
    EXPECT_NEAR(analysis::windDirectionField(north).values[0], 180.0f, 1e-3);
    // Wind toward the east (u>0) comes FROM the west -> 270 deg.
    auto east =
        makeWind(3, 1000, [](double, double) { return 5.0; }, [](double, double) { return 0.0; });
    EXPECT_NEAR(analysis::windDirectionField(east).values[0], 270.0f, 1e-3);
}

// The scalar helpers are what a single sampled value goes through -- a profile
// table cell, a cursor readout. If they and the field versions ever disagreed the
// symptom would be a table that contradicts the map it was picked from, so the
// field versions are built on the scalars and this pins that they still are.
TEST(Derived, WindFieldsAgreeWithTheScalarHelpersAtEveryCell) {
    auto w = makeWind(
        5, 1000, [](double x, double y) { return 0.001 * x - 0.002 * y; },
        [](double x, double y) { return 0.003 * x + 0.0005 * y; });
    const auto sp = analysis::windSpeedField(w);
    const auto dir = analysis::windDirectionField(w);
    for (std::size_t k = 0; k < sp.values.size(); ++k) {
        const float u = w.u.values[k], v = w.v.values[k];
        EXPECT_FLOAT_EQ(sp.values[k], analysis::windSpeedFrom(u, v));
        EXPECT_FLOAT_EQ(dir.values[k], analysis::windDirectionFrom(u, v));
    }
}

// Calm air reports 0, not the 180° ("from the south") that atan2(-0, -0) gives,
// and the result always stays in [0, 360).
TEST(Derived, CalmWindHasDirectionZeroAndDirectionsStayBelow360) {
    EXPECT_EQ(analysis::windDirectionFrom(0.0f, 0.0f), 0.0f);
    EXPECT_EQ(analysis::windDirectionFrom(-0.0f, -0.0f), 0.0f);
    // Blowing toward the south from very slightly east of north.
    const float nearlyNorth = analysis::windDirectionFrom(1e-30f, -5.0f);
    EXPECT_GE(nearlyNorth, 0.0f);
    EXPECT_LT(nearlyNorth, 360.0f);
}

TEST(Derived, ScalarWindHelpersYieldNaNWhenEitherComponentIsMissing) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(analysis::windSpeedFrom(nan, 1.0f)));
    EXPECT_TRUE(std::isnan(analysis::windSpeedFrom(1.0f, nan)));
    EXPECT_TRUE(std::isnan(analysis::windDirectionFrom(nan, 1.0f)));
    EXPECT_TRUE(std::isnan(analysis::windDirectionFrom(1.0f, nan)));
}

TEST(Derived, PotentialTemperature) {
    core::Field2D t;
    t.grid = metreGrid(3, 1000);
    t.values.assign(9, 250.0f);
    // At 1000 hPa, theta == T.
    EXPECT_NEAR(analysis::potentialTemperatureField(t, 1000.0).values[0], 250.0f, 1e-3);
    // At 500 hPa, theta = 250 * (1000/500)^0.2854 = 250 * 1.2185 ~ 304.6.
    EXPECT_NEAR(analysis::potentialTemperatureField(t, 500.0).values[0], 304.6f, 0.5f);
}

TEST(Derived, AsTemperatureKelvinGuardsUnitsAndIdentity) {
    core::Field2D base;
    base.grid = metreGrid(2, 1000);
    base.values = {250.0f, 260.0f, 270.0f, 280.0f};

    // A Kelvin temperature passes through unchanged.
    core::Field2D k = base;
    k.meta.standardName = "air_temperature";
    k.meta.units = "K";
    auto kk = analysis::asTemperatureKelvin(k);
    ASSERT_TRUE(kk.has_value());
    EXPECT_FLOAT_EQ(kk->values[0], 250.0f);

    // A Celsius field is converted to Kelvin.
    core::Field2D c = base;
    c.meta.varName = "t";
    c.meta.units = "degC";
    c.values = {0.0f, 10.0f, 20.0f, 30.0f};
    auto ck = analysis::asTemperatureKelvin(c);
    ASSERT_TRUE(ck.has_value());
    EXPECT_NEAR(ck->values[0], 273.15f, 1e-3);
    EXPECT_NEAR(ck->values[3], 303.15f, 1e-3);

    // A non-temperature field (geopotential height) is rejected outright.
    core::Field2D z = base;
    z.meta.standardName = "geopotential_height";
    z.meta.varName = "gh";
    z.meta.units = "gpm";
    EXPECT_FALSE(analysis::asTemperatureKelvin(z).has_value());
}

TEST(Derived, VorticityOfSolidBodyRotation) {
    // u = -omega*y, v = omega*x  ->  zeta = dv/dx - du/dy = omega + omega = 2*omega.
    const double omega = 1e-4;  // 1/s
    auto w = makeWind(
        7, 5000, [omega](double, double y) { return -omega * y; },
        [omega](double x, double) { return omega * x; });
    const auto vo = analysis::relativeVorticityField(w);
    // Interior cell (3,3).
    const float z = vo.values[3 * 7 + 3];
    EXPECT_FALSE(std::isnan(z));
    EXPECT_NEAR(z, 2.0 * omega, 1e-9);
    // Boundary cells are NaN (no central difference).
    EXPECT_TRUE(std::isnan(vo.values[0]));
}

TEST(Derived, DivergenceOfRadialFlow) {
    // u = a*x, v = a*y  ->  divergence = a + a = 2a.
    const double a = 2e-5;
    auto w = makeWind(
        7, 5000, [a](double x, double) { return a * x; }, [a](double, double y) { return a * y; });
    const auto dv = analysis::divergenceField(w);
    const float d = dv.values[3 * 7 + 3];
    EXPECT_NEAR(d, 2.0 * a, 1e-11);
    // A pure rotation has zero divergence.
    auto rot = makeWind(
        7, 5000, [](double, double y) { return -1e-4 * y; },
        [](double x, double) { return 1e-4 * x; });
    EXPECT_NEAR(analysis::divergenceField(rot).values[3 * 7 + 3], 0.0f, 1e-10);
}

// Vorticity and divergence differentiate along grid rows and columns, so they
// need the components resolved on those axes. The app once handed them
// earth-rotated components, which mixes the two by the local meridian
// convergence. Here a purely divergent flow sits where the polar-stereographic
// grid is turned ~27° from true north: the answer must be the same whichever
// frame the components arrive in.
TEST(Derived, VorticityAndDivergenceAreTheSameFromEitherFrame) {
    const double a = 2e-5;
    auto w = makeWind(
        7, 5000, [a](double x, double) { return a * x; }, [a](double, double y) { return a * y; });
    for (auto* f : {&w.u, &w.v}) {
        auto& g = std::get<core::ProjectedGrid>(f->grid);
        g.x0 = 1.0e6;  // lon = atan2(x, -y) ≈ 26.6° east of lon_0
        g.y0 = -2.0e6;
    }
    analysis::WindField earth = w;
    analysis::rotateToEarthRelative(earth);
    ASSERT_FALSE(earth.u.meta.gridRelativeWind);
    ASSERT_GT(std::abs(earth.u.values[24] - w.u.values[24]), 0.1f) << "the rotation is real";

    const int c = 3 * 7 + 3;
    for (const auto* in : {&w, &earth}) {
        EXPECT_NEAR(analysis::relativeVorticityField(*in).values[c], 0.0, 1e-9);
        EXPECT_NEAR(analysis::divergenceField(*in).values[c], 2.0 * a, 1e-9);
    }
}

// The frame flag travels with the rotation, so rotating twice is rotating once,
// and the inverse brings the decoded components back.
TEST(Derived, WindRotationTracksItsFrame) {
    auto w =
        makeWind(5, 5000, [](double, double) { return 10.0; }, [](double, double) { return 0.0; });
    for (auto* f : {&w.u, &w.v}) {
        auto& g = std::get<core::ProjectedGrid>(f->grid);
        g.x0 = 1.0e6;
        g.y0 = -2.0e6;
    }
    analysis::WindField once = w;
    analysis::rotateToEarthRelative(once);
    analysis::WindField twice = once;
    analysis::rotateToEarthRelative(twice);
    EXPECT_EQ(once.u.values, twice.u.values);

    analysis::rotateToGridRelative(twice);
    EXPECT_TRUE(twice.u.meta.gridRelativeWind);
    for (std::size_t k = 0; k < w.u.values.size(); ++k) {
        EXPECT_NEAR(twice.u.values[k], w.u.values[k], 1e-4);
        EXPECT_NEAR(twice.v.values[k], w.v.values[k], 1e-4);
    }
}

// On a grid whose columns span the globe, the first and last columns are
// neighbours; treating them as a boundary left a two-column NaN stripe through
// Greenwich on ERA5.
TEST(Derived, VorticityIsDefinedAcrossTheLongitudeSeam) {
    core::RegularLatLonGrid g;
    g.nlon = 8;
    g.nlat = 5;
    g.lon0 = 0.0;
    g.dlon = 45.0;
    g.lat0 = 20.0;
    g.dlat = -10.0;
    g.globalWrapLon = true;
    analysis::WindField w;
    w.u.grid = g;
    w.v.grid = g;
    // v = sin(lon): a meridional wind whose east-west gradient is the vorticity.
    w.u.values.assign(40, 0.0f);
    w.v.values.resize(40);
    for (int j = 0; j < 5; ++j)
        for (int i = 0; i < 8; ++i)
            w.v.values[static_cast<std::size_t>(j * 8 + i)] =
                static_cast<float>(std::sin(i * 45.0 * std::numbers::pi / 180.0));

    const auto vo = analysis::relativeVorticityField(w);
    const auto dv = analysis::divergenceField(w);
    for (int i : {0, 7}) {
        EXPECT_FALSE(std::isnan(vo.values[static_cast<std::size_t>(2 * 8 + i)])) << "column " << i;
        EXPECT_FALSE(std::isnan(dv.values[static_cast<std::size_t>(2 * 8 + i)])) << "column " << i;
    }
    // Column 0 is the mirror of column 4 (sin is odd about 180°), so the
    // across-the-seam difference has the opposite sign and equal size.
    EXPECT_NEAR(vo.values[2 * 8 + 0], -vo.values[2 * 8 + 4], 1e-12);
}

TEST(Derived, SphericalMetricTermOnLatLonGrid) {
    // Constant wind (u=10, v=0) on a lat/lon grid: every finite difference is
    // zero, so vorticity reduces to the spherical metric term u*tan(phi)/R and
    // divergence to -v*tan(phi)/R = 0. This exercises the lat/lon path (the
    // other tests use a metre grid, where no metric term applies).
    core::RegularLatLonGrid g;
    g.nlon = 5;
    g.nlat = 5;
    g.lon0 = 0.0;
    g.lat0 = 50.0;
    g.dlon = 2.5;
    g.dlat = -2.5;  // row j=2 -> lat 45 (N->S like ERA5)
    g.globalWrapLon = false;
    analysis::WindField w;
    w.u.grid = g;
    w.v.grid = g;
    w.u.values.assign(25, 10.0f);
    w.v.values.assign(25, 0.0f);

    const double R = 6371229.0;
    const double phi = 45.0 * std::numbers::pi / 180.0;
    const auto vo = analysis::relativeVorticityField(w);
    const auto dv = analysis::divergenceField(w);
    EXPECT_NEAR(vo.values[2 * 5 + 2], 10.0 * std::tan(phi) / R, 1e-12);  // interior at lat 45
    EXPECT_NEAR(dv.values[2 * 5 + 2], 0.0, 1e-12);
}

// asTemperatureKelvin reads units through the units module, so the CF spelling
// "degree_Celsius" converts instead of passing through as if it were Kelvin.
TEST(Derived, AsTemperatureKelvinConvertsTheCfCelsiusSpelling) {
    core::Field2D t;
    t.grid = metreGrid(2, 1000);
    t.meta.varName = "t";
    t.meta.units = "degree_Celsius";
    t.values = {15.0f, 15.0f, 15.0f, 15.0f};
    const auto k = analysis::asTemperatureKelvin(t);
    ASSERT_TRUE(k.has_value());
    EXPECT_NEAR(k->values[0], 288.15f, 1e-3);
}
