#include <gtest/gtest.h>

#include <filesystem>
#include <variant>

#include "viewer/core/grid.h"
#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture(const char* n) {
    return std::filesystem::path(MET_FIXTURE_DIR) / n;
}
}  // namespace

TEST(GribLambert, DecodesProjectedGridValues) {
    auto ds = readers::openDataset(fixture("lambert_sfc.grib2"));
    EXPECT_EQ(ds->formatName(), "GRIB");
    const auto& v = ds->catalog().variables().front();
    auto f = ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), -1});

    ASSERT_EQ(f.width(), 16);
    ASSERT_EQ(f.height(), 12);
    // The generator wrote value(i,j) = 250 + 0.5*i + 0.3*j.
    EXPECT_NEAR(f.at(0, 0), 250.0f, 1e-2);
    EXPECT_NEAR(f.at(15, 0), 257.5f, 1e-2);
    EXPECT_NEAR(f.at(0, 11), 253.3f, 1e-2);
    EXPECT_NEAR(f.at(15, 11), 260.8f, 1e-2);
}

TEST(GribLambert, GridIsProjectedAndAnchored) {
    auto ds = readers::openDataset(fixture("lambert_sfc.grib2"));
    const auto& v = ds->catalog().variables().front();
    auto f = ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), -1});

    // The grid must be a ProjectedGrid, and grid point (0,0) must map back to the
    // GRIB's stated first grid point (lat 30, lon 250 E == -110 W).
    ASSERT_TRUE(std::holds_alternative<core::ProjectedGrid>(f.grid));
    const core::LatLon nw = core::indexToLatLon(f.grid, 0, 0);
    EXPECT_NEAR(nw.lat, 30.0, 1e-3);
    EXPECT_NEAR(nw.lon, -110.0, 1e-3);
}
