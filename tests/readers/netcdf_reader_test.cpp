#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

#include "viewer/core/grid.h"
#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}
core::VerticalLevel hPa(double v) { return {core::VerticalLevel::Type::PressureHPa, v}; }
}  // namespace

TEST(CfReader, OpensAndCatalogs) {
    auto ds = readers::openDataset(fixture("era5_t_pl.nc"));
    EXPECT_EQ(ds->formatName(), "NetCDF/CF");
    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->units, "K");
    ASSERT_EQ(v->levels.size(), 9u);
    // Sorted high altitude (low pressure) first.
    EXPECT_DOUBLE_EQ(v->levels.front().value, 100.0);
    EXPECT_DOUBLE_EQ(v->levels.back().value, 1000.0);
    EXPECT_EQ(v->times.size(), 2u);
    // Relative humidity is a second griddable variable.
    EXPECT_NE(ds->catalog().find("r"), nullptr);
}

TEST(CfReader, DecodesPackedShortsWithScaleOffset) {
    auto ds = readers::openDataset(fixture("era5_t_pl.nc"));
    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    // t @ 500 hPa, first time: base(lat,lon) = 273.15 + 0.1*lon - 0.2*lat.
    auto f = ds->readField(core::FieldKey{"t", hPa(500), v->times.front(), -1});
    ASSERT_EQ(f.width(), 16);
    ASSERT_EQ(f.height(), 8);
    EXPECT_NEAR(f.at(0, 0), 259.15f, 1e-2);    // (70,0)
    EXPECT_NEAR(f.at(15, 7), 264.95f, 1e-2);   // (56,30)
}

TEST(CfReader, LevelTermAndFillValue) {
    auto ds = readers::openDataset(fixture("era5_t_pl.nc"));
    const auto* v = ds->catalog().find("t");
    // 850 hPa, first time: base + 0.06*(850-500) = base + 21 at every cell.
    auto f0 = ds->readField(core::FieldKey{"t", hPa(850), v->times.front(), -1});
    EXPECT_NEAR(f0.at(0, 0), 280.15f, 1e-2);  // base(70,0)=259.15 + 21
    // 850 hPa, second time: the (0,0) cell was written as _FillValue -> NaN.
    auto f1 = ds->readField(core::FieldKey{"t", hPa(850), v->times.back(), -1});
    EXPECT_TRUE(std::isnan(f1.at(0, 0)));
    // A non-fill cell in the same slab is finite.
    EXPECT_FALSE(std::isnan(f1.at(5, 3)));
}

TEST(CfReader, GridIsNorthToSouth) {
    auto ds = readers::openDataset(fixture("era5_t_pl.nc"));
    const auto* v = ds->catalog().find("t");
    auto f = ds->readField(core::FieldKey{"t", hPa(500), v->times.front(), -1});
    const core::LatLon nw = core::indexToLatLon(f.grid, 0, 0);
    EXPECT_DOUBLE_EQ(nw.lat, 70.0);   // row 0 is the northernmost latitude
    EXPECT_DOUBLE_EQ(nw.lon, 0.0);
}

// The whole point of M2: the same field read from GRIB and from NetCDF is
// identical, cell for cell.
TEST(Equivalence, GribAndNetcdfMatchAt500hPa) {
    auto grib = readers::openDataset(fixture("regular_ll_t500.grib2"));
    auto nc = readers::openDataset(fixture("era5_t_pl.nc"));

    const auto* gv = grib->catalog().find("t");
    const auto* nv = nc->catalog().find("t");
    ASSERT_NE(gv, nullptr);
    ASSERT_NE(nv, nullptr);

    auto gf = grib->readField(core::FieldKey{"t", hPa(500), gv->times.front(), -1});
    auto nf = nc->readField(core::FieldKey{"t", hPa(500), nv->times.front(), -1});

    ASSERT_EQ(gf.width(), nf.width());
    ASSERT_EQ(gf.height(), nf.height());
    for (int r = 0; r < gf.height(); ++r)
        for (int c = 0; c < gf.width(); ++c)
            EXPECT_NEAR(gf.at(c, r), nf.at(c, r), 2e-2) << "cell " << c << "," << r;
}
