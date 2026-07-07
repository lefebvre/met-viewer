#include <gtest/gtest.h>

#include <cmath>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/core/geo.h"
#include "viewer/core/grid.h"

using namespace met;

namespace {
// A field whose value equals a linear function of lat/lon plus a per-call bias.
core::Field2D linearField(double bias) {
    core::RegularLatLonGrid g;
    g.lat0 = 70; g.lon0 = 0; g.dlat = -2; g.dlon = 2; g.nlon = 16; g.nlat = 8;
    core::Field2D f;
    f.grid = g;
    f.meta.units = "K";
    f.values.resize(16u * 8u);
    for (int r = 0; r < 8; ++r)
        for (int c = 0; c < 16; ++c) {
            const double lat = 70 - 2.0 * r, lon = 2.0 * c;
            f.values[static_cast<std::size_t>(r) * 16u + static_cast<std::size_t>(c)] =
                static_cast<float>(273.15 + 0.1 * lon - 0.2 * lat + bias);
        }
    return f;
}
}  // namespace

TEST(Geo, GreatCircleDistance) {
    // ~111 km per degree of latitude.
    EXPECT_NEAR(core::greatCircleKm({0, 0}, {1, 0}), 111.19, 0.5);
    EXPECT_NEAR(core::greatCircleKm({0, 0}, {0, 0}), 0.0, 1e-9);
}

TEST(Geo, PathSamplingEvenSpacing) {
    const core::SampledPath sp = core::sampleGreatCirclePath({{0, 0}, {0, 10}}, 11);
    ASSERT_EQ(sp.points.size(), 11u);
    EXPECT_NEAR(sp.distancesKm.front(), 0.0, 1e-6);
    // Evenly spaced: consecutive gaps equal.
    const double gap = sp.distancesKm[1] - sp.distancesKm[0];
    for (std::size_t i = 2; i < sp.distancesKm.size(); ++i)
        EXPECT_NEAR(sp.distancesKm[i] - sp.distancesKm[i - 1], gap, 1e-6);
}

TEST(CrossSection, SamplesEachLevelAlongPath) {
    std::vector<std::pair<double, core::Field2D>> stack = {
        {500.0, linearField(0.0)}, {850.0, linearField(21.0)}};
    const auto cs = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 50);
    ASSERT_EQ(cs.pressures.size(), 2u);
    ASSERT_EQ(cs.values.size(), 2u);
    EXPECT_EQ(cs.values[0].size(), 50u);
    EXPECT_EQ(cs.units, "K");
    // At the first path point (68,4): base = 273.15 + 0.4 - 13.6 = 259.95.
    EXPECT_NEAR(cs.values[0].front(), 259.95f, 1e-2);         // 500 hPa, bias 0
    EXPECT_NEAR(cs.values[1].front(), 259.95f + 21.0f, 1e-2); // 850 hPa, bias 21
    // Distance increases monotonically.
    EXPECT_GT(cs.distancesKm.back(), cs.distancesKm.front());
}

TEST(Sounding, DewpointFromRH) {
    // RH 100% -> dewpoint equals temperature.
    EXPECT_NEAR(analysis::dewpointFromRH(293.15f, 100.0f), 293.15f, 0.2f);
    // RH < 100% -> dewpoint below temperature.
    EXPECT_LT(analysis::dewpointFromRH(293.15f, 50.0f), 293.15f);
}

TEST(Sounding, ExtractsSortedProfileWithDewpoint) {
    std::vector<std::pair<double, core::Field2D>> t = {
        {850.0, linearField(21.0)}, {500.0, linearField(0.0)}};
    core::Field2D rhField = linearField(0.0);
    rhField.values.assign(rhField.values.size(), 60.0f);  // RH 60% everywhere
    std::vector<std::pair<double, core::Field2D>> rh = {{850.0, rhField}, {500.0, rhField}};

    const auto s = analysis::extractSounding(t, rh, {64, 12});
    ASSERT_EQ(s.levels.size(), 2u);
    // Sorted top (low pressure) to bottom.
    EXPECT_DOUBLE_EQ(s.levels.front().pressure, 500.0);
    EXPECT_DOUBLE_EQ(s.levels.back().pressure, 850.0);
    for (const auto& lvl : s.levels) {
        EXPECT_FALSE(std::isnan(lvl.tempK));
        EXPECT_FALSE(std::isnan(lvl.dewpointK));
        EXPECT_LT(lvl.dewpointK, lvl.tempK);  // 60% RH -> Td < T
    }
}

TEST(TimeSeries, SamplesEachTime) {
    core::TimePoint t0{100}, t1{200}, t2{300};
    std::vector<std::pair<core::TimePoint, core::Field2D>> stack = {
        {t0, linearField(0.0)}, {t1, linearField(1.0)}, {t2, linearField(2.0)}};
    const auto ts = analysis::extractTimeSeries(stack, {64, 12});
    ASSERT_EQ(ts.values.size(), 3u);
    // Bias increments by 1 each step at a fixed point.
    EXPECT_NEAR(ts.values[1] - ts.values[0], 1.0f, 1e-3);
    EXPECT_NEAR(ts.values[2] - ts.values[1], 1.0f, 1e-3);
    EXPECT_EQ(ts.units, "K");
}
