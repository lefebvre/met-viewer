#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"
#include "viewer/core/field.h"

using namespace met;
using namespace met::analysis;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// 3x3 grid over lat 2..0, lon 0..2, scanning south & east. Every cell carries
// `value`, so a sample anywhere inside is exactly that.
core::Field2D uniformField(float value, const std::string& units = "K") {
    core::RegularLatLonGrid g;
    g.lat0 = 2.0;
    g.lon0 = 0.0;
    g.dlat = -1.0;
    g.dlon = 1.0;
    g.nlon = 3;
    g.nlat = 3;
    core::Field2D f;
    f.grid = g;
    f.values.assign(9, value);
    f.meta.units = units;
    return f;
}

core::VerticalLevel pressure(double hPa) { return {core::VerticalLevel::Type::PressureHPa, hPa}; }

ProfileColumn variableColumn(const std::string& id, const std::string& units = "K") {
    return {id, id, units, ProfileColumnKind::Variable};
}

core::VariableEntry entry(const std::string& name, const std::string& units,
                          const std::vector<double>& levelsHpa) {
    core::VariableEntry v;
    v.varName = name;
    v.units = units;
    for (double hPa : levelsHpa) v.levels.push_back(pressure(hPa));
    return v;
}

// A point comfortably inside uniformField's domain.
constexpr core::LatLon kInside{1.0, 1.0};

}  // namespace

TEST(MakeProfile, OrdersRowsGroundFirstSoASiteTableReadsFromTheSurfaceUp) {
    // Deliberately shuffled going in: the caller should not have to pre-sort, and
    // the order is the profile's contract rather than the caller's.
    const std::vector<core::VerticalLevel> levels = {pressure(500), pressure(1000), pressure(850)};
    const PointProfile p = makeProfile(levels, {variableColumn("t")}, kInside);

    ASSERT_EQ(p.levels.size(), 3u);
    EXPECT_DOUBLE_EQ(p.levels[0].level.value, 1000.0);  // ground
    EXPECT_DOUBLE_EQ(p.levels[1].level.value, 850.0);
    EXPECT_DOUBLE_EQ(p.levels[2].level.value, 500.0);  // aloft
}

TEST(MakeProfile, TakesEachRowsPressureFromTheLevelItselfOnAnIsobaricAxis) {
    const PointProfile p = makeProfile({pressure(700)}, {variableColumn("t")}, kInside);
    ASSERT_EQ(p.levels.size(), 1u);
    EXPECT_DOUBLE_EQ(p.levels[0].pressureHpa, 700.0);
    EXPECT_EQ(p.levels[0].pressureStatus, CellStatus::Ok);
}

TEST(MakeProfile, LeavesEveryCellNoRecordUntilAColumnIsFilled) {
    const PointProfile p = makeProfile({pressure(1000), pressure(500)},
                                       {variableColumn("t"), variableColumn("r")}, kInside);
    for (const ProfileLevel& row : p.levels) {
        ASSERT_EQ(row.statuses.size(), 2u);
        for (const CellStatus st : row.statuses) EXPECT_EQ(st, CellStatus::NoRecord);
    }
    EXPECT_FALSE(p.pointInDomain);
}

// The headline contract. sampleBilinear returns NaN for a point outside the grid
// and for missing data alike, so a table built naively on it cannot tell the user
// whether to move the point or distrust the file.
TEST(FillProfileColumn, TellsAPointOutsideTheGridApartFromMissingDataInsideIt) {
    const ProfileStack stack = {{1000.0, uniformField(280.0f)}};

    PointProfile inside = makeProfile({pressure(1000)}, {variableColumn("t")}, kInside);
    fillProfileColumn(inside, 0, stack);
    EXPECT_EQ(inside.levels[0].statuses[0], CellStatus::Ok);
    EXPECT_FLOAT_EQ(inside.levels[0].values[0], 280.0f);
    EXPECT_TRUE(inside.pointInDomain);

    // Far outside the 0..2 degree domain.
    PointProfile outside =
        makeProfile({pressure(1000)}, {variableColumn("t")}, core::LatLon{80.0, 170.0});
    fillProfileColumn(outside, 0, stack);
    EXPECT_EQ(outside.levels[0].statuses[0], CellStatus::OutsideGrid);
    EXPECT_FALSE(outside.pointInDomain);

    // In domain, but the data there is missing.
    const ProfileStack allMissing = {{1000.0, uniformField(kNaN)}};
    PointProfile missing = makeProfile({pressure(1000)}, {variableColumn("t")}, kInside);
    fillProfileColumn(missing, 0, allMissing);
    EXPECT_EQ(missing.levels[0].statuses[0], CellStatus::Missing);
    EXPECT_TRUE(missing.pointInDomain);
}

TEST(FillProfileColumn, MarksALevelTheColumnHasNoSlabForAsNoRecordNotAsMissingData) {
    // The row set spans three levels; this column only has two of them.
    const ProfileStack stack = {{1000.0, uniformField(280.0f)}, {500.0, uniformField(250.0f)}};
    PointProfile p =
        makeProfile({pressure(1000), pressure(850), pressure(500)}, {variableColumn("t")}, kInside);
    fillProfileColumn(p, 0, stack);

    EXPECT_EQ(p.levels[0].statuses[0], CellStatus::Ok);        // 1000
    EXPECT_EQ(p.levels[1].statuses[0], CellStatus::NoRecord);  // 850, absent
    EXPECT_EQ(p.levels[2].statuses[0], CellStatus::Ok);        // 500
}

TEST(FillProfileHeights, ConvertsEachSampleThroughItsOwnFieldsUnits) {
    // Geopotential, as ERA5 ships it: 9806.65 m2/s2 is 1000 gpm through g.
    const ProfileStack z = {{1000.0, uniformField(9806.65f, "m**2 s**-2")}};
    PointProfile p = makeProfile({pressure(1000)}, {}, kInside);
    fillProfileHeights(p, z, "z");

    EXPECT_EQ(p.levels[0].heightStatus, CellStatus::Ok);
    EXPECT_NEAR(p.levels[0].heightGpm, 1000.0f, 1e-2f);
    EXPECT_EQ(p.heightSourceVar, "z");
}

// toGeopotentialMeters refuses to guess, because 5000 is plausible in gpm and in
// m2/s2 alike. A blank altitude is the honest outcome; a guessed one would be a
// wrong altitude with nothing to mark it as such.
TEST(FillProfileHeights, LeavesTheAltitudeBlankWhenTheFieldsUnitsCannotBePlaced) {
    const ProfileStack z = {{1000.0, uniformField(5000.0f, "widgets")}};
    PointProfile p = makeProfile({pressure(1000)}, {}, kInside);
    fillProfileHeights(p, z, "gh");

    EXPECT_EQ(p.levels[0].heightStatus, CellStatus::Unconvertible);
    EXPECT_TRUE(std::isnan(p.levels[0].heightGpm));
}

TEST(FillProfilePressures, TakesEachRowsPressureFromThePresFieldOnANativeAxis) {
    const core::VerticalLevel m1{core::VerticalLevel::Type::Hybrid, 1.0};
    const core::VerticalLevel m2{core::VerticalLevel::Type::Hybrid, 2.0};
    // pres arrives in Pa, as GRIB carries it.
    const ProfileStack pres = {{1.0, uniformField(95000.0f, "Pa")},
                               {2.0, uniformField(85000.0f, "Pa")}};
    PointProfile p = makeProfile({m1, m2}, {}, kInside);
    // No pressure until it is read: a model level number is not a pressure.
    EXPECT_EQ(p.levels[0].pressureStatus, CellStatus::NoRecord);

    fillProfilePressures(p, pres);
    // Ground first means the higher-index hybrid level leads.
    EXPECT_DOUBLE_EQ(p.levels[0].pressureHpa, 850.0);
    EXPECT_DOUBLE_EQ(p.levels[1].pressureHpa, 950.0);
    EXPECT_EQ(p.levels[0].pressureStatus, CellStatus::Ok);
}

TEST(FillProfileWind, DerivesSpeedAndDirectionWithNorthAtZeroAndTheDirectionItBlowsFrom) {
    // A pure westerly: blowing towards the east, so it comes FROM 270.
    const ProfileStack u = {{850.0, uniformField(10.0f, "m/s")}};
    const ProfileStack v = {{850.0, uniformField(0.0f, "m/s")}};
    PointProfile p =
        makeProfile({pressure(850)},
                    {{kWindSpeedId, "Wind speed", "m/s", ProfileColumnKind::WindSpeed},
                     {kWindDirectionId, "Wind direction", "deg", ProfileColumnKind::WindDirection}},
                    kInside);
    fillProfileWind(p, u, v);

    EXPECT_EQ(p.levels[0].statuses[0], CellStatus::Ok);
    EXPECT_FLOAT_EQ(p.levels[0].values[0], 10.0f);
    EXPECT_FLOAT_EQ(p.levels[0].values[1], 270.0f);
}

TEST(FillProfileWind, FillsBothDerivedColumnsFromOneUvPair) {
    const ProfileStack u = {{850.0, uniformField(0.0f, "m/s")}};
    const ProfileStack v = {{850.0, uniformField(-5.0f, "m/s")}};  // from the north
    PointProfile p =
        makeProfile({pressure(850)},
                    {{kWindSpeedId, "Wind speed", "m/s", ProfileColumnKind::WindSpeed},
                     {kWindDirectionId, "Wind direction", "deg", ProfileColumnKind::WindDirection}},
                    kInside);
    fillProfileWind(p, u, v);

    EXPECT_FLOAT_EQ(p.levels[0].values[0], 5.0f);
    EXPECT_FLOAT_EQ(p.levels[0].values[1], 0.0f);
    EXPECT_EQ(p.levels[0].statuses[0], CellStatus::Ok);
    EXPECT_EQ(p.levels[0].statuses[1], CellStatus::Ok);
}

TEST(FillProfileWind, LeavesALevelWithOnlyOneComponentAsNoRecord) {
    const ProfileStack u = {{850.0, uniformField(10.0f, "m/s")},
                            {500.0, uniformField(20.0f, "m/s")}};
    const ProfileStack v = {{850.0, uniformField(0.0f, "m/s")}};  // no 500
    PointProfile p =
        makeProfile({pressure(850), pressure(500)},
                    {{kWindSpeedId, "Wind speed", "m/s", ProfileColumnKind::WindSpeed}}, kInside);
    fillProfileWind(p, u, v);

    EXPECT_EQ(p.levels[0].statuses[0], CellStatus::Ok);        // 850
    EXPECT_EQ(p.levels[1].statuses[0], CellStatus::NoRecord);  // 500, no v
}

TEST(ProfileColumnChoices, OffersOnlyVariablesWithARealProfileOnTheChosenAxis) {
    const std::vector<core::VariableEntry> vars = {
        entry("t", "K", {1000, 850, 500}),
        entry("r", "%", {1000, 850, 500}),
        entry("t2m", "K", {1000}),  // single level: not a profile
    };
    const ProfileColumnChoices c = profileColumnChoices(vars);

    EXPECT_EQ(c.levelType, core::VerticalLevel::Type::PressureHPa);
    ASSERT_EQ(c.available.size(), 2u);
    EXPECT_EQ(c.available[0].id, "t");
    EXPECT_EQ(c.available[1].id, "r");
    EXPECT_EQ(c.levels.size(), 3u);
    EXPECT_DOUBLE_EQ(c.levels.front().value, 1000.0);  // ground first here too
}

// Dropping a mismatched variable silently would leave the reader wondering why a
// field they can see on the map is absent from the picker.
TEST(ProfileColumnChoices, ReportsASingleLevelVariableAsUnavailableRatherThanOmittingIt) {
    const std::vector<core::VariableEntry> vars = {entry("t", "K", {1000, 850}),
                                                   entry("t2m", "K", {1000})};
    const ProfileColumnChoices c = profileColumnChoices(vars);

    ASSERT_EQ(c.unavailable.size(), 1u);
    EXPECT_EQ(c.unavailable[0].id, "t2m");
}

TEST(ProfileColumnChoices, ExcludesTheHeightVariableBecauseItIsAlreadyTheAltitudeCoordinate) {
    std::vector<core::VariableEntry> vars = {entry("t", "K", {1000, 850, 500}),
                                             entry("gh", "gpm", {1000, 850, 500})};
    vars[1].standardName = "geopotential_height";
    const ProfileColumnChoices c = profileColumnChoices(vars);

    EXPECT_EQ(c.heightVar, "gh");
    for (const ProfileColumn& col : c.available) EXPECT_NE(col.id, "gh");
}

TEST(ProfileColumnChoices, ReportsWindAvailableOnlyWhenBothComponentsHaveAProfile) {
    const std::vector<core::VariableEntry> paired = {entry("u", "m/s", {1000, 850}),
                                                     entry("v", "m/s", {1000, 850})};
    EXPECT_TRUE(profileColumnChoices(paired).windAvailable);

    const std::vector<core::VariableEntry> uOnly = {entry("u", "m/s", {1000, 850}),
                                                    entry("t", "K", {1000, 850})};
    EXPECT_FALSE(profileColumnChoices(uOnly).windAvailable);
}

TEST(ProfileColumnChoices, FallsBackToTheNativeModelAxisWhenNothingHasPressureLevels) {
    core::VariableEntry t;
    t.varName = "t";
    t.units = "K";
    for (double i : {1.0, 2.0, 3.0}) t.levels.push_back({core::VerticalLevel::Type::Hybrid, i});
    const ProfileColumnChoices c = profileColumnChoices({t});

    EXPECT_EQ(c.levelType, core::VerticalLevel::Type::Hybrid);
    ASSERT_EQ(c.levels.size(), 3u);
    EXPECT_DOUBLE_EQ(c.levels.front().value, 3.0);  // nearest the ground
}

TEST(ProfileColumnChoices, ReportsNoAxisForADatasetWithNoVerticalProfileAtAll) {
    const std::vector<core::VariableEntry> vars = {entry("t2m", "K", {1000})};
    const ProfileColumnChoices c = profileColumnChoices(vars);

    EXPECT_EQ(c.levelType, core::VerticalLevel::Type::Unknown);
    EXPECT_TRUE(c.available.empty());
    EXPECT_TRUE(c.levels.empty());
}
