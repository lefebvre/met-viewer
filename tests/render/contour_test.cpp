#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

#include "viewer/core/field.h"
#include "viewer/render/contour.h"

using namespace met;
using namespace met::render;

namespace {
// Build a field with value = f(col,row) on a trivial 1-degree grid.
template <typename F>
core::Field2D makeField(int w, int h, F fn) {
    core::RegularLatLonGrid g;
    g.lat0 = 0;
    g.lon0 = 0;
    g.dlat = -1;
    g.dlon = 1;
    g.nlon = w;
    g.nlat = h;
    core::Field2D f;
    f.grid = g;
    f.values.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            f.values[static_cast<std::size_t>(r) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(c)] = static_cast<float>(fn(c, r));
    return f;
}
}  // namespace

TEST(Contour, LinearRampCrossingLocation) {
    // value = col, so the isoline at 2.5 must sit at x = 2.5 everywhere.
    auto f = makeField(6, 4, [](int c, int) { return c; });
    auto segs = contourAt(f, 2.5);
    ASSERT_FALSE(segs.empty());
    for (const auto& s : segs) {
        EXPECT_NEAR(s.x0, 2.5, 1e-6);
        EXPECT_NEAR(s.x1, 2.5, 1e-6);
    }
}

TEST(Contour, NoLinesOutsideRange) {
    auto f = makeField(5, 5, [](int c, int) { return c; });  // range 0..4
    EXPECT_TRUE(contourAt(f, -1.0).empty());
    EXPECT_TRUE(contourAt(f, 99.0).empty());
}

TEST(Contour, SkipsNanCells) {
    auto f = makeField(4, 4, [](int c, int) { return c; });
    // Poison one cell's corner; cells touching it are skipped, but far cells
    // still produce the isoline.
    f.values[0] = std::numeric_limits<float>::quiet_NaN();  // (0,0)
    auto segs = contourAt(f, 2.5);
    EXPECT_FALSE(segs.empty());
}

TEST(Contour, LevelsRespectInterval) {
    auto f = makeField(10, 10, [](int c, int r) { return c + r; });  // range 0..18
    auto levels = contourLevels(f, 5.0);
    // Expect levels at 5, 10, 15.
    ASSERT_EQ(levels.size(), 3u);
    EXPECT_DOUBLE_EQ(levels[0].value, 5.0);
    EXPECT_DOUBLE_EQ(levels[1].value, 10.0);
    EXPECT_DOUBLE_EQ(levels[2].value, 15.0);
}

TEST(Contour, NiceInterval) {
    EXPECT_DOUBLE_EQ(niceContourInterval(0.0, 100.0, 10), 10.0);
    EXPECT_DOUBLE_EQ(niceContourInterval(0.0, 0.0, 10), 0.0);  // degenerate
}

// The isoline cache keys on Field2D::id rather than the field's address. Address
// identity looks equivalent right up until the allocator hands a new field the
// slot a freed one occupied, at which point the view keeps drawing the old
// contours over new data with nothing to indicate it.
TEST(Contour, CacheInvalidatesOnANewFieldAtTheSameAddress) {
    render::ContourCache cache;

    auto makeField = [](float scale) {
        auto f = std::make_unique<core::Field2D>();
        core::RegularLatLonGrid g;
        g.lat0 = 0;
        g.lon0 = 0;
        g.dlat = 1;
        g.dlon = 1;
        g.nlon = 8;
        g.nlat = 8;
        f->grid = g;
        f->values.resize(64);
        for (std::size_t j = 0; j < 8; ++j)
            for (std::size_t i = 0; i < 8; ++i)
                f->values[j * 8 + i] = scale * static_cast<float>(i);
        return f;
    };

    auto a = makeField(1.0f);
    const std::size_t levelsA = cache.levels(*a, 1.0).size();
    ASSERT_GT(levelsA, 0u);
    const void* addrA = a.get();
    a.reset();

    // A fresh field, quite possibly at the same address, with a different range.
    auto b = makeField(10.0f);
    const std::size_t levelsB = cache.levels(*b, 1.0).size();
    if (b.get() == addrA) {
        EXPECT_NE(levelsB, levelsA) << "same address must not serve the old field's isolines";
    }
    EXPECT_GT(levelsB, levelsA);  // 10x the range at the same interval => more lines
}

TEST(Contour, CacheReusesTheSameFieldAndInterval) {
    core::Field2D f;
    core::RegularLatLonGrid g;
    g.lat0 = 0;
    g.lon0 = 0;
    g.dlat = 1;
    g.dlon = 1;
    g.nlon = 8;
    g.nlat = 8;
    f.grid = g;
    f.values.resize(64);
    for (std::size_t j = 0; j < 8; ++j)
        for (std::size_t i = 0; i < 8; ++i) f.values[j * 8 + i] = static_cast<float>(i);

    render::ContourCache cache;
    const auto* first = &cache.levels(f, 1.0);
    const auto* second = &cache.levels(f, 1.0);
    EXPECT_EQ(first, second);  // same storage: nothing was rebuilt
    EXPECT_EQ(first->size(), second->size());
}
