#include <gtest/gtest.h>

#include "viewer/core/units.h"

using namespace met::core;

TEST(Units, Temperature) {
    ASSERT_TRUE(convert(273.15, "K", "Cel").has_value());
    EXPECT_NEAR(*convert(273.15, "K", "Cel"), 0.0, 1e-9);
    EXPECT_NEAR(*convert(0.0, "Cel", "K"), 273.15, 1e-9);
    // Spelling variants.
    EXPECT_NEAR(*convert(300.0, "kelvin", "degC"), 26.85, 1e-9);
}

TEST(Units, Pressure) {
    EXPECT_NEAR(*convert(101325.0, "Pa", "hPa"), 1013.25, 1e-9);
    EXPECT_NEAR(*convert(500.0, "hPa", "Pa"), 50000.0, 1e-9);
}

TEST(Units, Wind) { EXPECT_NEAR(*convert(1.0, "m/s", "kt"), 1.9438444924406, 1e-9); }

TEST(Units, UnknownPairReturnsNullopt) { EXPECT_FALSE(convert(1.0, "K", "hPa").has_value()); }

TEST(Units, PreferredDisplay) {
    ASSERT_TRUE(preferredDisplayUnit("K").has_value());
    EXPECT_EQ(*preferredDisplayUnit("K"), "Cel");
    EXPECT_FALSE(preferredDisplayUnit("m/s").has_value());
}

// Geopotential (m2/s2) is NOT geopotential height (gpm): they differ by g. Aliasing
// them mislabelled ERA5's `z` by a factor of ~9.81 in any converted readout.
TEST(Units, GeopotentialIsDistinctFromGeopotentialHeight) {
    ASSERT_TRUE(convert(9806.65, "m2/s2", "gpm").has_value());
    EXPECT_NEAR(*convert(9806.65, "m2/s2", "gpm"), 1000.0, 1e-6);
    EXPECT_NEAR(*convert(1000.0, "gpm", "m2/s2"), 9806.65, 1e-6);
    EXPECT_NEAR(*convert(9806.65, "m2/s2", "dam"), 100.0, 1e-6);
    // ecCodes spells it "m**2 s**-2".
    EXPECT_NEAR(*convert(9806.65, "m**2 s**-2", "gpm"), 1000.0, 1e-6);
    // Geopotential reads naturally as a height, so offer that as the display unit.
    ASSERT_TRUE(preferredDisplayUnit("m2/s2").has_value());
    EXPECT_EQ(*preferredDisplayUnit("m2/s2"), "gpm");
}

// A dimensionless "1" is used for masks, fractions and ratios alike, so it must
// not be silently treated as a mixing ratio and offered a x1000 conversion.
TEST(Units, DimensionlessOneIsNotAMixingRatio) {
    EXPECT_FALSE(convert(0.5, "1", "g/kg").has_value());
    EXPECT_FALSE(preferredDisplayUnit("1").has_value());
    // The real mixing-ratio spellings still convert.
    EXPECT_NEAR(*convert(0.001, "kg/kg", "g/kg"), 1.0, 1e-12);
    EXPECT_NEAR(*convert(0.001, "kg kg**-1", "g/kg"), 1.0, 1e-12);
}
