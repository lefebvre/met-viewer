#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"
#include "viewer/app/extractions.h"
#include "viewer/app/jobs.h"
#include "viewer/readers/detect.h"
#include "viewer/readers/ireader.h"

using namespace met;

namespace {

std::filesystem::path fixture(const char* n) { return std::filesystem::path(MET_FIXTURE_DIR) / n; }

// era5_t_pl.nc: t (K, packed), r (%), z (m2/s2), u and v (m/s) over nine
// pressure levels and two times, lat 70..56 N, lon 0..30 E, all analytic.
std::unique_ptr<readers::IDataset> openEra5() {
    return readers::openDataset(fixture("era5_t_pl.nc"));
}

// A point inside the fixture's domain, away from its edges.
constexpr core::LatLon kInside{63.0, 10.0};

core::TimePoint firstTime(readers::IDataset& ds) { return ds.catalog().find("t")->times.front(); }

}  // namespace

TEST(ComputePointProfile, ReadsEveryRequestedColumnOffTheEra5Fixture) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const analysis::PointProfile p =
        app::extractions::computePointProfile(*ds, firstTime(*ds), -1, kInside, {"t", "r"});

    EXPECT_EQ(p.levelType, core::VerticalLevel::Type::PressureHPa);
    ASSERT_EQ(p.levels.size(), 9u);
    ASSERT_EQ(p.columns.size(), 2u);
    EXPECT_EQ(p.columns[0].id, "t");
    EXPECT_EQ(p.columns[1].id, "r");
    EXPECT_TRUE(p.pointInDomain);

    // Ground first: 1000 hPa leads, 100 hPa closes.
    EXPECT_DOUBLE_EQ(p.levels.front().level.value, 1000.0);
    EXPECT_DOUBLE_EQ(p.levels.back().level.value, 100.0);

    // r = 40 + 40*(plev/1000), so 80% at the surface and 44% at 100 hPa.
    EXPECT_NEAR(p.levels.front().values[1], 80.0f, 1e-3f);
    EXPECT_NEAR(p.levels.back().values[1], 44.0f, 1e-3f);
    for (const analysis::ProfileLevel& row : p.levels)
        EXPECT_EQ(row.statuses[0], analysis::CellStatus::Ok);
}

TEST(ComputePointProfile, MatchesTheFixturesAnalyticTemperatureAtAKnownPoint) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const analysis::PointProfile p =
        app::extractions::computePointProfile(*ds, firstTime(*ds), -1, kInside, {"t"});

    // base = 273.15 + 0.1*lon - 0.2*lat; t = base + 0.06*(plev - 500) at time 0.
    const double base = 273.15 + 0.1 * kInside.lon - 0.2 * kInside.lat;
    for (const analysis::ProfileLevel& row : p.levels) {
        const double expected = base + 0.06 * (row.level.value - 500.0);
        // The fixture packs t as a scaled short, so the round trip is coarse.
        EXPECT_NEAR(row.values[0], static_cast<float>(expected), 0.01f)
            << "at " << row.level.value << " hPa";
    }
}

// z is the file's geopotential in m2/s2. It becomes the altitude coordinate, not
// a column, and it arrives as MSL geopotential metres.
TEST(ComputePointProfile, TakesTheAltitudeFromTheHeightFieldAndNamesWhereItCameFrom) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const analysis::PointProfile p =
        app::extractions::computePointProfile(*ds, firstTime(*ds), -1, kInside, {"t"});

    EXPECT_EQ(p.heightSourceVar, "z");
    for (const analysis::ProfileColumn& c : p.columns) EXPECT_NE(c.id, "z");
    // Height increases as pressure falls, and the surface is near sea level.
    EXPECT_LT(p.levels.front().heightGpm, 200.0f);
    EXPECT_GT(p.levels.back().heightGpm, 15000.0f);
    for (std::size_t i = 1; i < p.levels.size(); ++i)
        EXPECT_GT(p.levels[i].heightGpm, p.levels[i - 1].heightGpm);
}

TEST(ComputePointProfile, DerivesWindSpeedAndDirectionFromTheFixturesUvPair) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const analysis::PointProfile p = app::extractions::computePointProfile(
        *ds, firstTime(*ds), -1, kInside,
        {std::string(analysis::kWindSpeedId), std::string(analysis::kWindDirectionId)});

    ASSERT_EQ(p.columns.size(), 2u);
    EXPECT_EQ(p.columns[0].kind, analysis::ProfileColumnKind::WindSpeed);
    EXPECT_EQ(p.columns[1].kind, analysis::ProfileColumnKind::WindDirection);

    // u = 5 + 0.02*(1000 - plev), v = -3 throughout.
    for (const analysis::ProfileLevel& row : p.levels) {
        const double u = 5.0 + 0.02 * (1000.0 - row.level.value);
        EXPECT_NEAR(row.values[0], static_cast<float>(std::hypot(u, -3.0)), 1e-3f);
        // Blowing east and south, so it comes from the west-northwest quadrant.
        EXPECT_GT(row.values[1], 270.0f);
        EXPECT_LT(row.values[1], 360.0f);
    }
    // Stronger aloft, as the fixture builds it.
    EXPECT_GT(p.levels.back().values[0], p.levels.front().values[0]);
}

// The progress bar is sized from the estimate before any I/O starts. If the two
// ever drift the bar silently lies, and nothing else in the app would notice.
TEST(EstimatePointProfileReads, MatchesTheSlabsComputePointProfileActuallyReads) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const std::vector<std::vector<std::string>> selections = {
        {"t"},
        {"t", "r"},
        {std::string(analysis::kWindSpeedId)},
        {"t", "r", std::string(analysis::kWindSpeedId), std::string(analysis::kWindDirectionId)},
    };
    for (const std::vector<std::string>& columns : selections) {
        auto progress = std::make_shared<app::JobProgress>();
        const int estimate = app::extractions::estimatePointProfileReads(*ds, columns);
        const analysis::PointProfile p = app::extractions::computePointProfile(
            *ds, firstTime(*ds), -1, kInside, columns, progress);
        EXPECT_FALSE(p.levels.empty());
        EXPECT_EQ(progress->done.load(), estimate) << "for " << columns.size() << " column(s)";
        EXPECT_GT(estimate, 0);
    }
}

// Speed and direction come out of the same two slabs, so asking for both must not
// double the reads.
TEST(EstimatePointProfileReads, ReadsTheUvPairOnceWhetherOneOrBothWindColumnsAreAsked) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const int one =
        app::extractions::estimatePointProfileReads(*ds, {std::string(analysis::kWindSpeedId)});
    const int both = app::extractions::estimatePointProfileReads(
        *ds, {std::string(analysis::kWindSpeedId), std::string(analysis::kWindDirectionId)});
    EXPECT_EQ(one, both);
}

TEST(ComputePointProfile, ReportsEveryCellOutsideTheGridForAPointOffTheDomain) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    // The fixture spans lat 56..70 N, lon 0..30 E.
    const analysis::PointProfile p = app::extractions::computePointProfile(
        *ds, firstTime(*ds), -1, core::LatLon{-40.0, 200.0}, {"t", "r"});

    ASSERT_FALSE(p.levels.empty());
    EXPECT_FALSE(p.pointInDomain);
    for (const analysis::ProfileLevel& row : p.levels)
        for (const analysis::CellStatus st : row.statuses)
            EXPECT_EQ(st, analysis::CellStatus::OutsideGrid);
}

// A single-level file has no vertical profile to table. An empty result is the
// honest answer; a one-row table would imply a profile that is not there.
TEST(ComputePointProfile, ReturnsNothingForADatasetWithNoVerticalAxis) {
    auto ds = readers::openDataset(fixture("wind_uv_850.grib2"));
    ASSERT_TRUE(ds);
    const core::TimePoint t = ds->catalog().variables().front().times.front();
    const analysis::PointProfile p =
        app::extractions::computePointProfile(*ds, t, -1, core::LatLon{45.0, 10.0}, {"u", "v"});

    EXPECT_TRUE(p.levels.empty());
    EXPECT_TRUE(p.columns.empty());
    EXPECT_EQ(app::extractions::estimatePointProfileReads(*ds, {"u", "v"}), 0);
}

// A column selection restored from settings can name a variable the newly opened
// file does not have. Dropping it beats adding a column of blanks.
TEST(ComputePointProfile, DropsARequestedColumnTheDatasetCannotOffer) {
    auto ds = openEra5();
    ASSERT_TRUE(ds);
    const analysis::PointProfile p = app::extractions::computePointProfile(
        *ds, firstTime(*ds), -1, kInside, {"t", "no_such_variable", "r"});

    ASSERT_EQ(p.columns.size(), 2u);
    EXPECT_EQ(p.columns[0].id, "t");
    EXPECT_EQ(p.columns[1].id, "r");
}
