#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "viewer/analysis/heights.h"

using namespace met;

namespace {
core::VariableEntry entry(const std::string& name, const std::string& units,
                          const std::string& standardName, int nLevels) {
    core::VariableEntry v;
    v.varName = name;
    v.units = units;
    v.standardName = standardName;
    for (int i = 0; i < nLevels; ++i)
        v.levels.push_back({core::VerticalLevel::Type::PressureHPa, 1000.0 - 100.0 * i});
    return v;
}
}  // namespace

TEST(HeightVariable, FindsGeopotentialHeightByStandardName) {
    const std::vector<core::VariableEntry> vars = {entry("t", "K", "air_temperature", 5),
                                                   entry("HGT", "gpm", "geopotential_height", 5)};
    const auto found = analysis::findHeightVariable(vars);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, "HGT");
}

TEST(HeightVariable, PrefersGeopotentialHeightOverGeopotential) {
    // ERA5-shaped files can carry both; the height needs no conversion.
    const std::vector<core::VariableEntry> vars = {entry("z", "m**2 s**-2", "geopotential", 5),
                                                   entry("gh", "gpm", "", 5)};
    const auto found = analysis::findHeightVariable(vars);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, "gh");
}

TEST(HeightVariable, AcceptsGeopotentialAlone) {
    const std::vector<core::VariableEntry> vars = {entry("t", "K", "air_temperature", 5),
                                                   entry("z", "m**2 s**-2", "geopotential", 5)};
    EXPECT_EQ(analysis::findHeightVariable(vars).value_or(""), "z");
}

TEST(HeightVariable, RejectsSingleLevelAndUnrelatedLengthFields) {
    const std::vector<core::VariableEntry> vars = {
        entry("orog", "m", "surface_altitude", 1),         // terrain: one level, not a profile
        entry("cloud_top", "m", "cloud_top_altitude", 5),  // a length, but not the axis
        entry("t", "K", "air_temperature", 5),
    };
    EXPECT_FALSE(analysis::findHeightVariable(vars).has_value());
}

TEST(HeightVariable, RejectsHeightNamesInUnplaceableUnits) {
    // A "gh" in units nothing can convert would land the profile at an arbitrary
    // altitude; better to report no height axis at all.
    const std::vector<core::VariableEntry> vars = {entry("gh", "furlong", "", 5)};
    EXPECT_FALSE(analysis::findHeightVariable(vars).has_value());
}
