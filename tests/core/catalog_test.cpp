#include <gtest/gtest.h>

#include "viewer/core/catalog.h"

using namespace met::core;

namespace {
VerticalLevel pressure(double hPa) { return {VerticalLevel::Type::PressureHPa, hPa}; }
}  // namespace

TEST(Catalog, SortsLevelsHighToLowAltitude) {
    DatasetCatalog cat;
    // Add out of order: 500, 850, 250 hPa.
    cat.addRecord("t", "Temperature", "K", "air_temperature", pressure(500), TimePoint{100}, -1, 10);
    cat.addRecord("t", "Temperature", "K", "air_temperature", pressure(850), TimePoint{100}, -1, 20);
    cat.addRecord("t", "Temperature", "K", "air_temperature", pressure(250), TimePoint{100}, -1, 30);
    cat.finalize();

    const VariableEntry* v = cat.find("t");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->levels.size(), 3u);
    // Ascending pressure = high altitude first: 250, 500, 850.
    EXPECT_DOUBLE_EQ(v->levels[0].value, 250.0);
    EXPECT_DOUBLE_EQ(v->levels[1].value, 500.0);
    EXPECT_DOUBLE_EQ(v->levels[2].value, 850.0);
}

TEST(Catalog, ResolveReturnsCorrectHandle) {
    DatasetCatalog cat;
    cat.addRecord("t", "Temperature", "K", "", pressure(500), TimePoint{100}, -1, 111);
    cat.addRecord("t", "Temperature", "K", "", pressure(850), TimePoint{100}, -1, 222);
    cat.finalize();

    FieldKey k500{"t", pressure(500), TimePoint{100}, -1};
    FieldKey k850{"t", pressure(850), TimePoint{100}, -1};
    ASSERT_TRUE(cat.resolve(k500).has_value());
    EXPECT_EQ(*cat.resolve(k500), 111u);
    EXPECT_EQ(*cat.resolve(k850), 222u);

    FieldKey missing{"t", pressure(300), TimePoint{100}, -1};
    EXPECT_FALSE(cat.resolve(missing).has_value());
}

TEST(Catalog, MultipleVariablesAndTimes) {
    DatasetCatalog cat;
    cat.addRecord("t", "Temperature", "K", "", pressure(500), TimePoint{100}, -1, 1);
    cat.addRecord("t", "Temperature", "K", "", pressure(500), TimePoint{200}, -1, 2);
    cat.addRecord("u", "U wind", "m/s", "", pressure(500), TimePoint{100}, -1, 3);
    cat.finalize();

    EXPECT_EQ(cat.variables().size(), 2u);
    const VariableEntry* t = cat.find("t");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->times.size(), 2u);
    EXPECT_EQ(*cat.resolve(FieldKey{"t", pressure(500), TimePoint{200}, -1}), 2u);
}
