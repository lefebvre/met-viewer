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

TEST(Units, Wind) {
    EXPECT_NEAR(*convert(1.0, "m/s", "kt"), 1.9438444924406, 1e-9);
}

TEST(Units, UnknownPairReturnsNullopt) {
    EXPECT_FALSE(convert(1.0, "K", "hPa").has_value());
}

TEST(Units, PreferredDisplay) {
    ASSERT_TRUE(preferredDisplayUnit("K").has_value());
    EXPECT_EQ(*preferredDisplayUnit("K"), "Cel");
    EXPECT_FALSE(preferredDisplayUnit("m/s").has_value());
}
