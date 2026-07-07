#include <gtest/gtest.h>

#include <cmath>

#include "viewer/analysis/derived.h"
#include "viewer/analysis/wind.h"
#include "viewer/core/grid.h"

using namespace met;

namespace {
// A projected grid in metres so finite differences are exact (no cos-lat metric).
core::ProjectedGrid metreGrid(int n, double spacing) {
    core::ProjectedGrid g;
    g.crs = core::Crs("+proj=stere +lat_0=90 +lat_ts=60 +lon_0=0 +R=6371229 +units=m +no_defs");
    g.nx = n; g.ny = n; g.dx = spacing; g.dy = spacing;
    g.x0 = 0; g.y0 = 0;
    return g;
}

// Build a wind field from analytic u(i,j), v(i,j) on a metre grid.
template <typename FU, typename FV>
analysis::WindField makeWind(int n, double spacing, FU fu, FV fv) {
    analysis::WindField w;
    core::ProjectedGrid g = metreGrid(n, spacing);
    w.u.grid = g; w.v.grid = g;
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
    auto w = makeWind(5, 1000, [](double, double) { return 3.0; },
                      [](double, double) { return 4.0; });
    const auto sp = analysis::windSpeedField(w);
    EXPECT_EQ(sp.meta.varName, "wspd");
    for (float v : sp.values) EXPECT_FLOAT_EQ(v, 5.0f);  // 3-4-5
}

TEST(Derived, WindDirection) {
    // Wind blowing toward the north (v>0) comes FROM the south -> 180 deg.
    auto north = makeWind(3, 1000, [](double, double) { return 0.0; },
                          [](double, double) { return 5.0; });
    EXPECT_NEAR(analysis::windDirectionField(north).values[0], 180.0f, 1e-3);
    // Wind toward the east (u>0) comes FROM the west -> 270 deg.
    auto east = makeWind(3, 1000, [](double, double) { return 5.0; },
                         [](double, double) { return 0.0; });
    EXPECT_NEAR(analysis::windDirectionField(east).values[0], 270.0f, 1e-3);
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

TEST(Derived, VorticityOfSolidBodyRotation) {
    // u = -omega*y, v = omega*x  ->  zeta = dv/dx - du/dy = omega + omega = 2*omega.
    const double omega = 1e-4;  // 1/s
    auto w = makeWind(7, 5000, [omega](double, double y) { return -omega * y; },
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
    auto w = makeWind(7, 5000, [a](double x, double) { return a * x; },
                      [a](double, double y) { return a * y; });
    const auto dv = analysis::divergenceField(w);
    const float d = dv.values[3 * 7 + 3];
    EXPECT_NEAR(d, 2.0 * a, 1e-11);
    // A pure rotation has zero divergence.
    auto rot = makeWind(7, 5000, [](double, double y) { return -1e-4 * y; },
                        [](double x, double) { return 1e-4 * x; });
    EXPECT_NEAR(analysis::divergenceField(rot).values[3 * 7 + 3], 0.0f, 1e-10);
}
