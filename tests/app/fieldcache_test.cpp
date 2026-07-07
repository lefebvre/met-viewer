#include <gtest/gtest.h>

#include <memory>

#include "viewer/app/fieldcache.h"
#include "viewer/core/field.h"

using namespace met;

namespace {
std::shared_ptr<core::Field2D> makeField(int cells, float value) {
    auto f = std::make_shared<core::Field2D>();
    core::RegularLatLonGrid g;
    g.nlon = cells;
    g.nlat = 1;
    f->grid = g;
    f->values.assign(static_cast<std::size_t>(cells), value);
    return f;
}
core::FieldKey key(const char* var, double level, long time) {
    return {var, {core::VerticalLevel::Type::PressureHPa, level}, core::TimePoint{time}, -1};
}
}  // namespace

TEST(FieldCache, GetPutRoundTrip) {
    app::FieldCache cache(1024 * 1024);
    EXPECT_EQ(cache.get(key("t", 500, 0)), nullptr);
    auto f = makeField(100, 1.0f);
    cache.put(key("t", 500, 0), f);
    EXPECT_TRUE(cache.contains(key("t", 500, 0)));
    EXPECT_EQ(cache.get(key("t", 500, 0)), f);
    EXPECT_EQ(cache.count(), 1u);
    EXPECT_EQ(cache.sizeBytes(), 100u * sizeof(float));
}

TEST(FieldCache, DistinctKeys) {
    app::FieldCache cache(1024 * 1024);
    cache.put(key("t", 500, 0), makeField(10, 1.0f));
    cache.put(key("t", 850, 0), makeField(10, 2.0f));
    cache.put(key("t", 500, 100), makeField(10, 3.0f));
    cache.put(key("u", 500, 0), makeField(10, 4.0f));
    EXPECT_EQ(cache.count(), 4u);
}

TEST(FieldCache, EvictsLeastRecentlyUsed) {
    // Budget for exactly two 100-cell fields.
    const std::size_t two = 2 * 100 * sizeof(float);
    app::FieldCache cache(two);
    cache.put(key("t", 500, 0), makeField(100, 1.0f));  // A
    cache.put(key("t", 500, 1), makeField(100, 2.0f));  // B
    // Touch A so B is the least-recently-used.
    EXPECT_NE(cache.get(key("t", 500, 0)), nullptr);
    cache.put(key("t", 500, 2), makeField(100, 3.0f));  // C -> evicts B
    EXPECT_TRUE(cache.contains(key("t", 500, 0)));   // A kept (recently used)
    EXPECT_FALSE(cache.contains(key("t", 500, 1)));  // B evicted
    EXPECT_TRUE(cache.contains(key("t", 500, 2)));   // C present
    EXPECT_EQ(cache.count(), 2u);
}

TEST(FieldCache, ShrinkingBudgetEvicts) {
    app::FieldCache cache(1024 * 1024);
    for (int i = 0; i < 5; ++i) cache.put(key("t", 500, i), makeField(100, float(i)));
    EXPECT_EQ(cache.count(), 5u);
    cache.setBudgetBytes(150 * sizeof(float));  // room for one 100-cell field
    EXPECT_EQ(cache.count(), 1u);
    // The most recent (i=4) survives.
    EXPECT_TRUE(cache.contains(key("t", 500, 4)));
}

TEST(FieldCache, ReplaceUpdatesSize) {
    app::FieldCache cache(1024 * 1024);
    cache.put(key("t", 500, 0), makeField(100, 1.0f));
    cache.put(key("t", 500, 0), makeField(10, 2.0f));  // replace with smaller
    EXPECT_EQ(cache.count(), 1u);
    EXPECT_EQ(cache.sizeBytes(), 10u * sizeof(float));
    EXPECT_FLOAT_EQ(cache.get(key("t", 500, 0))->values[0], 2.0f);
}
