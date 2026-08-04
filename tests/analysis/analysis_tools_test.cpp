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
    g.lat0 = 70;
    g.lon0 = 0;
    g.dlat = -2;
    g.dlon = 2;
    g.nlon = 16;
    g.nlat = 8;
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
    std::vector<std::pair<double, core::Field2D>> stack = {{500.0, linearField(0.0)},
                                                           {850.0, linearField(21.0)}};
    const auto cs = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 50);
    ASSERT_EQ(cs.pressures.size(), 2u);
    ASSERT_EQ(cs.values.size(), 2u);
    EXPECT_EQ(cs.values[0].size(), 50u);
    EXPECT_EQ(cs.units, "K");
    // At the first path point (68,4): base = 273.15 + 0.4 - 13.6 = 259.95.
    EXPECT_NEAR(cs.values[0].front(), 259.95f, 1e-2);          // 500 hPa, bias 0
    EXPECT_NEAR(cs.values[1].front(), 259.95f + 21.0f, 1e-2);  // 850 hPa, bias 21
    // Distance increases monotonically.
    EXPECT_GT(cs.distancesKm.back(), cs.distancesKm.front());
}

TEST(CrossSection, SortsUnorderedLevelsByPressure) {
    // The extractor must sort by pressure ascending (top first) regardless of the
    // caller's order, since the view's log-p mapping assumes a monotonic axis.
    std::vector<std::pair<double, core::Field2D>> stack = {
        {500.0, linearField(5.0)}, {1000.0, linearField(10.0)}, {250.0, linearField(2.0)}};
    const auto cs = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 16);
    ASSERT_EQ(cs.pressures.size(), 3u);
    // pressures is now per-column; an isobaric level broadcasts the same value.
    EXPECT_DOUBLE_EQ(cs.pressures[0].front(), 250.0);
    EXPECT_DOUBLE_EQ(cs.pressures[1].front(), 500.0);
    EXPECT_DOUBLE_EQ(cs.pressures[2].front(), 1000.0);
    // Each row's values track its own level's bias (2 at 250, 10 at 1000).
    EXPECT_GT(cs.values[2].front(), cs.values[0].front());
}

TEST(Sounding, DewpointFromRH) {
    // RH 100% -> dewpoint equals temperature.
    EXPECT_NEAR(analysis::dewpointFromRH(293.15f, 100.0f), 293.15f, 0.2f);
    // RH < 100% -> dewpoint below temperature.
    EXPECT_LT(analysis::dewpointFromRH(293.15f, 50.0f), 293.15f);
}

TEST(Sounding, ExtractsSortedProfileWithDewpoint) {
    std::vector<std::pair<double, core::Field2D>> t = {{850.0, linearField(21.0)},
                                                       {500.0, linearField(0.0)}};
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

TEST(Sounding, ExtractsWindProfileWhenUVPresent) {
    std::vector<std::pair<double, core::Field2D>> t = {{850.0, linearField(21.0)},
                                                       {500.0, linearField(0.0)}};
    const std::vector<std::pair<double, core::Field2D>> rh;  // no humidity

    core::Field2D uField = linearField(0.0), vField = linearField(0.0);
    uField.values.assign(uField.values.size(), 10.0f);  // 10 m/s eastward
    vField.values.assign(vField.values.size(), -5.0f);  // 5 m/s southward
    const std::vector<std::pair<double, core::Field2D>> u = {{850.0, uField}, {500.0, uField}};
    const std::vector<std::pair<double, core::Field2D>> v = {{850.0, vField}, {500.0, vField}};

    const auto s = analysis::extractSounding(t, rh, {64, 12}, u, v);
    ASSERT_EQ(s.levels.size(), 2u);
    for (const auto& lvl : s.levels) {
        // Regular lat/lon grid: earth-relative components equal the grid-relative ones.
        EXPECT_NEAR(lvl.windU, 10.0f, 1e-3f);
        EXPECT_NEAR(lvl.windV, -5.0f, 1e-3f);
    }

    // Without U/V stacks the wind stays NaN (the profile is data-dependent).
    const auto sNoWind = analysis::extractSounding(t, rh, {64, 12});
    for (const auto& lvl : sNoWind.levels) {
        EXPECT_TRUE(std::isnan(lvl.windU));
        EXPECT_TRUE(std::isnan(lvl.windV));
    }
}

TEST(Sounding, ReadsGeopotentialHeightThroughItsOwnUnits) {
    std::vector<std::pair<double, core::Field2D>> t = {{850.0, linearField(21.0)},
                                                       {500.0, linearField(0.0)}};
    const std::vector<std::pair<double, core::Field2D>> rh;

    // Geopotential (m2/s2), as ERA5 ships it: 5500 gpm at 500 hPa, 1500 at 850.
    core::Field2D z500 = linearField(0.0), z850 = linearField(0.0);
    z500.meta.units = z850.meta.units = "m**2 s**-2";
    z500.values.assign(z500.values.size(), static_cast<float>(5500.0 * 9.80665));
    z850.values.assign(z850.values.size(), static_cast<float>(1500.0 * 9.80665));
    const std::vector<std::pair<double, core::Field2D>> z = {{850.0, z850}, {500.0, z500}};

    const auto s = analysis::extractSounding(t, rh, {64, 12}, {}, {}, z);
    ASSERT_EQ(s.levels.size(), 2u);
    EXPECT_NEAR(s.levels.front().heightGpm, 5500.0f, 0.5f);  // 500 hPa
    EXPECT_NEAR(s.levels.back().heightGpm, 1500.0f, 0.5f);   // 850 hPa

    // No height stack -> no height, rather than a guess from the temperature trace.
    const auto noZ = analysis::extractSounding(t, rh, {64, 12});
    for (const auto& lvl : noZ.levels) EXPECT_TRUE(std::isnan(lvl.heightGpm));
}

TEST(Sounding, ModelLevelsCarryHeight) {
    core::Field2D pres = linearField(0.0);
    pres.meta.units = "hPa";
    core::Field2D p1 = pres, p2 = pres;
    p1.values.assign(p1.values.size(), 500.0f);
    p2.values.assign(p2.values.size(), 850.0f);
    const std::vector<std::pair<double, core::Field2D>> presStack = {{1.0, p1}, {2.0, p2}};
    const std::vector<std::pair<double, core::Field2D>> t = {{1.0, linearField(0.0)},
                                                             {2.0, linearField(21.0)}};

    core::Field2D z1 = linearField(0.0), z2 = linearField(0.0);
    z1.meta.units = z2.meta.units = "gpm";
    z1.values.assign(z1.values.size(), 5500.0f);
    z2.values.assign(z2.values.size(), 1500.0f);
    const std::vector<std::pair<double, core::Field2D>> z = {{1.0, z1}, {2.0, z2}};

    const auto s = analysis::extractSoundingModelLevels(t, presStack, {}, {64, 12}, {}, {}, z);
    ASSERT_EQ(s.levels.size(), 2u);
    EXPECT_NEAR(s.levels.front().heightGpm, 5500.0f, 0.5f);  // sorted top-down: 500 hPa
    EXPECT_NEAR(s.levels.back().heightGpm, 1500.0f, 0.5f);
}

TEST(CrossSection, HeightsFollowTheLevelOrderAndConvertToGpm) {
    std::vector<std::pair<double, core::Field2D>> stack = {{850.0, linearField(21.0)},
                                                           {500.0, linearField(0.0)}};
    core::Field2D z500 = linearField(0.0), z850 = linearField(0.0);
    z500.meta.units = z850.meta.units = "dam";  // decametres, as height charts are drawn
    z500.values.assign(z500.values.size(), 550.0f);
    z850.values.assign(z850.values.size(), 150.0f);
    const std::vector<std::pair<double, core::Field2D>> z = {{850.0, z850}, {500.0, z500}};

    const auto cs = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 20, z);
    ASSERT_EQ(cs.heights.size(), 2u);
    ASSERT_EQ(cs.heights.front().size(), 20u);
    // Levels are sorted top-first, and heights ride along with them.
    EXPECT_DOUBLE_EQ(cs.pressures.front().front(), 500.0);
    EXPECT_NEAR(cs.heights.front().front(), 5500.0, 1e-6);
    EXPECT_NEAR(cs.heights.back().front(), 1500.0, 1e-6);

    // A section built without a height stack reports none at all, so a view can
    // check one condition instead of scanning for finite values.
    const auto noZ = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 20);
    EXPECT_TRUE(noZ.heights.empty());
}

TEST(CrossSection, HeightsInUnplaceableUnitsAreDroppedNotGuessed) {
    std::vector<std::pair<double, core::Field2D>> stack = {{500.0, linearField(0.0)},
                                                           {850.0, linearField(21.0)}};
    core::Field2D bogus = linearField(0.0);
    bogus.meta.units = "furlong";
    bogus.values.assign(bogus.values.size(), 5500.0f);
    const std::vector<std::pair<double, core::Field2D>> z = {{500.0, bogus}, {850.0, bogus}};

    const auto cs = analysis::extractCrossSection(stack, {{68, 4}, {58, 26}}, 20, z);
    EXPECT_TRUE(cs.heights.empty());
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
