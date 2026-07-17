#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <span>
#include <variant>

#include "viewer/core/grid.h"
#include "viewer/readers/arl/arldataset.h"
#include "viewer/readers/arl/arlreader.h"
#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture(const char* n) {
    return std::filesystem::path(MET_FIXTURE_DIR) / n;
}
}  // namespace

TEST(Arl, ProbeAcceptsAlphaGridId) {
    // Real HRRR ARL files carry a 2-char alphanumeric grid ID (e.g. "AA") in the
    // label's grid field (bytes 12-13), not digits. The probe must accept it.
    readers::arl::ArlReader reader;
    const char* label = "26 714 0 1 0AAINDX";  // 18 bytes: time/level fields, grid "AA", INDX
    const auto* bytes = reinterpret_cast<const std::byte*>(label);
    EXPECT_GT(reader.probe(std::span<const std::byte>(bytes, std::strlen(label)), "x_hrrr"), 0);
}

TEST(Arl, DecodesExtendedGridDimensions) {
    // Grid IDs below 64 (digits, '@') add nothing; the I3 header value stands.
    EXPECT_EQ(readers::arl::decodeGridDim(59, static_cast<unsigned char>('0')), 59);
    EXPECT_EQ(readers::arl::decodeGridDim(100, static_cast<unsigned char>('@')), 100);
    // HRRR: grid "AA" ('A' == 65) lifts 799 -> 1799 and 59 -> 1059.
    EXPECT_EQ(readers::arl::decodeGridDim(799, static_cast<unsigned char>('A')), 1799);
    EXPECT_EQ(readers::arl::decodeGridDim(59, static_cast<unsigned char>('A')), 1059);
    // Each step above 'A' adds another 1000 (e.g. 'C' -> +3000).
    EXPECT_EQ(readers::arl::decodeGridDim(200, static_cast<unsigned char>('C')), 3200);
}

TEST(Arl, DetectsAndCatalogs) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    EXPECT_EQ(ds->formatName(), "ARL");
    // Surface T02M -> t2m, and 1000 hPa TEMP -> t.
    EXPECT_NE(ds->catalog().find("t2m"), nullptr);
    const auto* t = ds->catalog().find("t");
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->levels.size(), 1u);
    EXPECT_EQ(t->levels.front().type, core::VerticalLevel::Type::PressureHPa);
    EXPECT_DOUBLE_EQ(t->levels.front().value, 1000.0);
}

TEST(Arl, UnpacksKnownAnalyticField) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    const auto* v = ds->catalog().find("t2m");
    ASSERT_NE(v, nullptr);
    auto f = ds->readField(core::FieldKey{"t2m", v->levels.front(), v->times.front(), -1});
    ASSERT_EQ(f.width(), 20);
    ASSERT_EQ(f.height(), 10);
    // value(i,j) = 273 + 0.5*i + 0.3*j, recovered within the 1-byte packing
    // precision (scale 2^(7-3) = 16 -> ~0.03).
    EXPECT_NEAR(f.at(0, 0), 273.0f, 1e-3);   // == VAR1, stored exactly
    EXPECT_NEAR(f.at(19, 0), 282.5f, 0.05f);
    EXPECT_NEAR(f.at(0, 9), 275.7f, 0.05f);
    EXPECT_NEAR(f.at(19, 9), 285.2f, 0.05f);
}

TEST(Arl, LatLonGridGeometry) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    const auto* v = ds->catalog().find("t2m");
    auto f = ds->readField(core::FieldKey{"t2m", v->levels.front(), v->times.front(), -1});
    // size_km == 0 in the header -> regular lat/lon grid; sync (1,1) at (30,250).
    ASSERT_TRUE(std::holds_alternative<core::RegularLatLonGrid>(f.grid));
    const core::LatLon sw = core::indexToLatLon(f.grid, 0, 0);
    EXPECT_DOUBLE_EQ(sw.lat, 30.0);
    EXPECT_DOUBLE_EQ(sw.lon, 250.0);
    const core::LatLon ne = core::indexToLatLon(f.grid, 19, 9);
    EXPECT_DOUBLE_EQ(ne.lat, 48.0);
    EXPECT_DOUBLE_EQ(ne.lon, 288.0);
}
