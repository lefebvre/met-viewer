#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

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

// Everything a height axis can be spelled as lands in geopotential metres, and
// anything else lands nowhere rather than at a plausible wrong altitude.
TEST(Units, ToGeopotentialMeters) {
    EXPECT_NEAR(toGeopotentialMeters(5500.0, "gpm"), 5500.0, 1e-9);
    EXPECT_NEAR(toGeopotentialMeters(5500.0, "m"), 5500.0, 1e-9);  // geometric ~ geopotential
    EXPECT_NEAR(toGeopotentialMeters(550.0, "dam"), 5500.0, 1e-9);
    EXPECT_NEAR(toGeopotentialMeters(5500.0 * 9.80665, "m**2 s**-2"), 5500.0, 1e-6);
    EXPECT_TRUE(std::isnan(toGeopotentialMeters(5500.0, "K")));
    EXPECT_TRUE(std::isnan(toGeopotentialMeters(5500.0, "")));
}

// "gpdm" is a geopotential decametre — the unit a 500 hPa height chart is drawn
// in. Reading it as gpm puts the 500 hPa surface at 550 m.
TEST(Units, GeopotentialDecametresAreDecametres) {
    EXPECT_NEAR(*convert(550.0, "gpdm", "gpm"), 5500.0, 1e-9);
    EXPECT_NEAR(toGeopotentialMeters(550.0, "gpdm"), 5500.0, 1e-9);
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

// The wind-speed column of a point profile has to offer both m/s and knots, and
// nothing about that is specific to speed -- these are the units a reader picks
// between for temperature and pressure too.
TEST(AlternativeUnits, ListsTheNativeUnitFirstThenTheRestOfItsFamily) {
    const std::vector<std::string> speed = alternativeUnits("m/s");
    ASSERT_EQ(speed.size(), 2u);
    EXPECT_EQ(speed[0], "m/s");
    EXPECT_EQ(speed[1], "kt");

    // Asking from the other end of the same family flips which one leads.
    const std::vector<std::string> knots = alternativeUnits("kt");
    ASSERT_EQ(knots.size(), 2u);
    EXPECT_EQ(knots[0], "kt");
    EXPECT_EQ(knots[1], "m/s");

    const std::vector<std::string> temp = alternativeUnits("kelvin");  // canonicalized
    ASSERT_EQ(temp.size(), 2u);
    EXPECT_EQ(temp[0], "K");
    EXPECT_EQ(temp[1], "Cel");
}

// A caller builds a menu straight from the result, so a unit with nothing to
// offer must still yield one entry rather than an empty list to guard against.
TEST(AlternativeUnits, YieldsASingleEntryForAUnitWithNoAlternative) {
    const std::vector<std::string> pct = alternativeUnits("%");
    ASSERT_EQ(pct.size(), 1u);
    EXPECT_EQ(pct[0], "%");

    // Dimensionless "1" stays alone for the same reason it is not a mixing ratio.
    const std::vector<std::string> one = alternativeUnits("1");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "1");
}

// The whole point of listing an alternative is that the reader can switch to it.
// An entry convert() cannot reach would be a menu item that silently does
// nothing, so every unit reported here must actually convert both ways.
TEST(AlternativeUnits, OffersOnlyUnitsThatConvertActuallyAccepts) {
    for (const char* native : {"K", "Cel", "Pa", "hPa", "m/s", "kt", "gpm", "dam", "m2/s2", "kg/kg",
                               "g/kg", "m", "mm"}) {
        const std::vector<std::string> alts = alternativeUnits(native);
        ASSERT_FALSE(alts.empty()) << native;
        EXPECT_EQ(alts.front(), native) << native;
        for (const std::string& alt : alts) {
            EXPECT_TRUE(convert(1.0, native, alt).has_value()) << native << " -> " << alt;
            EXPECT_TRUE(convert(1.0, alt, native).has_value()) << alt << " -> " << native;
        }
    }
}

// Every spelling a file uses for one unit reads the same, in the shortest form
// that cannot be misread.
TEST(Units, LabelsEverySpellingOfAUnitTheSameWay) {
    EXPECT_EQ(unitLabel("m s**-1"), "m/s");
    EXPECT_EQ(unitLabel("m s-1"), "m/s");
    EXPECT_EQ(unitLabel("m s^-1"), "m/s");
    EXPECT_EQ(unitLabel("m/s"), "m/s");
    EXPECT_EQ(unitLabel("kg kg**-1"), "kg/kg");
    EXPECT_EQ(unitLabel("Pa s**-1"), "Pa/s");
    EXPECT_EQ(unitLabel("m**2 s**-2"), "m²/s²");
    EXPECT_EQ(unitLabel("m2/s2"), "m²/s²");
    EXPECT_EQ(unitLabel("kg m**-2"), "kg/m²");
    EXPECT_EQ(unitLabel("s**-1"), "1/s");
    EXPECT_EQ(unitLabel("Cel"), "°C");
    EXPECT_EQ(unitLabel("K"), "K");
}

// "kg/m²/s" would leave the reader working out what divides what.
TEST(Units, GroupsSeveralFactorsBelowOneSlash) {
    EXPECT_EQ(unitLabel("kg m-2 s-1"), "kg/(m² s)");
    EXPECT_EQ(unitLabel("K m**2 kg**-1 s**-1"), "K m²/(kg s)");
}

TEST(Units, LeavesAUnitItCannotParseAsItIs) {
    for (const char* raw :
         {"(10**-6 g) m**-3", "(0 - 1)", "1", "Proportion", "", "m/s/s", "10**-6 g"})
        EXPECT_EQ(unitLabel(raw), raw) << raw;
}

TEST(Units, SpellsAnExportedUnitInPlainAscii) {
    EXPECT_EQ(unitLabelAscii("kg m**-2 s**-1"), "kg/(m2 s)");
    EXPECT_EQ(unitLabelAscii("m s**-1"), "m/s");
    EXPECT_EQ(unitLabelAscii("Cel"), "degC");
    EXPECT_EQ(unitLabelAscii("K"), "K");
}
