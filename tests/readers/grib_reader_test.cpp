#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "viewer/core/grid.h"
#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture() {
    return std::filesystem::path(MET_FIXTURE_DIR) / "regular_ll_t500.grib2";
}
}  // namespace

TEST(GribReader, OpensAndCatalogs) {
    auto ds = readers::openDataset(fixture());
    EXPECT_EQ(ds->formatName(), "GRIB");
    const auto& cat = ds->catalog();
    ASSERT_EQ(cat.variables().size(), 1u);
    const auto& v = cat.variables().front();
    EXPECT_EQ(v.varName, "t");
    EXPECT_EQ(v.units, "K");
    ASSERT_EQ(v.levels.size(), 1u);
    EXPECT_EQ(v.levels.front().type, core::VerticalLevel::Type::PressureHPa);
    EXPECT_DOUBLE_EQ(v.levels.front().value, 500.0);
}

TEST(GribReader, DecodesKnownAnalyticField) {
    auto ds = readers::openDataset(fixture());
    const auto& v = ds->catalog().variables().front();
    core::FieldKey key{v.varName, v.levels.front(), v.times.front(), v.members.front()};
    core::Field2D f = ds->readField(key);

    ASSERT_EQ(f.width(), 16);
    ASSERT_EQ(f.height(), 8);

    // The generator wrote value(lat,lon) = 273.15 + 0.1*lon - 0.2*lat, with the
    // first grid point at (lat 70, lon 0) scanning south & east. Verify corners.
    // idx(0,0) = (70,0) -> 259.15
    EXPECT_NEAR(f.at(0, 0), 259.15f, 1e-3);
    // idx(15,0) = (70,30) -> 262.15
    EXPECT_NEAR(f.at(15, 0), 262.15f, 1e-3);
    // idx(0,7) = (56,0) -> 261.95
    EXPECT_NEAR(f.at(0, 7), 261.95f, 1e-3);
    // idx(15,7) = (56,30) -> 264.95
    EXPECT_NEAR(f.at(15, 7), 264.95f, 1e-3);
}

TEST(GribReader, HonorsMissingValuesWithoutBitmap) {
    // regular_ll_missing.grib2 encodes three missing cells via complex-packing
    // missing-value management (data template 5.3) with NO bitmap. Those cells
    // must decode to NaN rather than leaking the 9999 sentinel as real data,
    // and genuine values must stay finite. Regression: missing was honored only
    // when a bitmap was present.
    auto ds = readers::openDataset(std::filesystem::path(MET_FIXTURE_DIR) / "regular_ll_missing.grib2");
    const auto& v = ds->catalog().variables().front();
    core::Field2D f =
        ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), v.members.front()});
    ASSERT_EQ(f.width(), 4);
    ASSERT_EQ(f.height(), 4);
    // Scan-order missing indices 5, 6, 10 -> (col,row) (1,1), (2,1), (2,2).
    EXPECT_TRUE(std::isnan(f.at(1, 1)));
    EXPECT_TRUE(std::isnan(f.at(2, 1)));
    EXPECT_TRUE(std::isnan(f.at(2, 2)));
    // Real cells stay finite (value 280 at scan index 0).
    EXPECT_FALSE(std::isnan(f.at(0, 0)));
    EXPECT_NEAR(f.at(0, 0), 280.0f, 1e-2);
}

TEST(GribReader, GridGeometryMatches) {
    auto ds = readers::openDataset(fixture());
    const auto& v = ds->catalog().variables().front();
    core::Field2D f =
        ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), -1});

    const core::LatLon nw = core::indexToLatLon(f.grid, 0, 0);
    EXPECT_DOUBLE_EQ(nw.lat, 70.0);
    EXPECT_DOUBLE_EQ(nw.lon, 0.0);
    const core::BBox b = core::gridBBox(f.grid);
    EXPECT_DOUBLE_EQ(b.minLat, 56.0);
    EXPECT_DOUBLE_EQ(b.maxLat, 70.0);
}
