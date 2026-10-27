#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

#include "tests/readers/nc_builder.h"
#include "viewer/core/grid.h"
#include "viewer/readers/detect.h"

using namespace met;
using met::testing::NcBuilder;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}
core::VerticalLevel hPa(double v) { return {core::VerticalLevel::Type::PressureHPa, v}; }
core::VerticalLevel heightM(double v) { return {core::VerticalLevel::Type::HeightM, v}; }
const core::VerticalLevel kSurface{core::VerticalLevel::Type::Surface, 0.0};

// The small grid every built file shares: 3 latitudes north to south, 4 longitudes.
constexpr std::size_t kNLat = 3, kNLon = 4;

struct Axes {
    int time = -1, lev = -1, lat = -1, lon = -1;
};

// Adds time(2), lev(3 pressure levels), lat and lon coordinates.
Axes standardAxes(NcBuilder& nc) {
    Axes a;
    a.time = nc.coord("time", {0.0, 6.0}, "hours since 2020-01-01 00:00:00");
    a.lev = nc.coord("lev", {1000.0, 850.0, 500.0}, "hPa");
    a.lat = nc.coord("lat", {20.0, 10.0, 0.0}, "degrees_north");
    a.lon = nc.coord("lon", {0.0, 10.0, 20.0, 30.0}, "degrees_east");
    return a;
}

// Values for an array of the given shape, where each element encodes its index
// along each axis as decimal digits: f(i0, i1, …) = i0*10^(n-1) + … + i(n-1).
std::vector<double> digits(const std::vector<std::size_t>& shape) {
    std::size_t total = 1;
    for (std::size_t n : shape) total *= n;
    std::vector<double> out(total);
    for (std::size_t flat = 0; flat < total; ++flat) {
        std::size_t rest = flat;
        double value = 0, place = 1;
        for (std::size_t a = shape.size(); a-- > 0;) {
            value += static_cast<double>(rest % shape[a]) * place;
            rest /= shape[a];
            place *= 10;
        }
        out[flat] = value;
    }
    return out;
}
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
    EXPECT_NEAR(f.at(0, 0), 259.15f, 1e-2);   // (70,0)
    EXPECT_NEAR(f.at(15, 7), 264.95f, 1e-2);  // (56,30)
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
    EXPECT_DOUBLE_EQ(nw.lat, 70.0);  // row 0 is the northernmost latitude
    EXPECT_DOUBLE_EQ(nw.lon, 0.0);
}

TEST(CfReader, NonPressureVerticalAxisIsNotCollapsed) {
    // A height vertical axis must be recognized as the level dimension so a
    // multi-level variable exposes all its levels. Regression: only pressure
    // axes were detected, collapsing every other vertical type to slice 0 and
    // mislabeling it "Surface".
    auto ds = readers::openDataset(fixture("height_levels.nc"));
    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->levels.size(), 4u);
    for (const auto& lvl : v->levels) EXPECT_EQ(lvl.type, core::VerticalLevel::Type::HeightM);
    // Height sorts high-altitude first.
    EXPECT_DOUBLE_EQ(v->levels.front().value, 500.0);
    EXPECT_DOUBLE_EQ(v->levels.back().value, 10.0);

    // The requested slab is read, not slice 0: t = 300 - 5*heightIndex + time,
    // and 100 m is height index 2 -> 290 at the first time.
    auto f = ds->readField(core::FieldKey{"t", heightM(100), v->times.front(), -1});
    ASSERT_EQ(f.width(), 4);
    ASSERT_EQ(f.height(), 4);
    EXPECT_NEAR(f.at(0, 0), 290.0f, 1e-3);
    EXPECT_NEAR(f.at(3, 3), 290.0f, 1e-3);
}

TEST(CfReader, IsoTimeSeparatorKeepsTimeOfDay) {
    // "hours since 2020-06-01T06:00:00" must keep the 06:00. Regression: the
    // scanf format only accepted a space separator, silently dropping the
    // time-of-day and shifting the base by up to 24 h.
    auto ds = readers::openDataset(fixture("height_levels.nc"));
    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->times.size(), 2u);
    // 2020-06-01T06:00:00Z == 1590991200; the +6 h step == 1591012800.
    EXPECT_EQ(v->times.front().epochSeconds, 1590991200);
    EXPECT_EQ(v->times.back().epochSeconds, 1591012800);
}

// The two readers must agree: the same field read from GRIB and from NetCDF is
// identical, cell for cell. This is what makes the format layer interchangeable
// to everything above it, so a regression here is a regression in the whole
// abstraction, not just in one reader.
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

// RegularLatLonGrid models a uniform axis. A Gaussian latitude axis is not one,
// and taking lat[1]-lat[0] as the spacing silently mis-places every row (worst
// toward the poles) with no symptom except values appearing in the wrong place —
// so the reader rejects it rather than pretending.
TEST(CfReader, RejectsNonUniformLatitudeAxis) {
    EXPECT_THROW((void)readers::openDataset(fixture("gaussian_lat.nc")), readers::ReadError);
}

TEST(CfReader, StillAcceptsTheUniformFixtures) {
    EXPECT_NO_THROW((void)readers::openDataset(fixture("era5_t_pl.nc")));
    EXPECT_NO_THROW((void)readers::openDataset(fixture("height_levels.nc")));
}

// MERRA-2 style: PS(time, lat, lon) beside T(time, lev, lat, lon). PS has no level
// dimension, so it is one single-level field — not one identical slab listed at
// each of the file's pressure levels, which drew a fake constant profile.
TEST(CfReader, SingleLevelVariableIsNotListedAtEveryLevel) {
    NcBuilder nc("single_level");
    const Axes a = standardAxes(nc);
    nc.put(nc.var("T", NC_FLOAT, {a.time, a.lev, a.lat, a.lon}), digits({2, 3, kNLat, kNLon}));
    nc.put(nc.var("PS", NC_FLOAT, {a.time, a.lat, a.lon}), digits({2, kNLat, kNLon}));
    auto ds = readers::openDataset(nc.finish());

    const auto* t = ds->catalog().find("T");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->levels.size(), 3u);

    const auto* ps = ds->catalog().find("PS");
    ASSERT_NE(ps, nullptr);
    ASSERT_EQ(ps->levels.size(), 1u);
    EXPECT_EQ(ps->levels.front().type, core::VerticalLevel::Type::Surface);
    EXPECT_FALSE(ds->catalog().resolve(core::FieldKey{"PS", hPa(500), ps->times.back(), -1}));

    // Second time, row 2, column 3 -> digits 1,2,3.
    const auto f = ds->readField(core::FieldKey{"PS", kSurface, ps->times.back(), -1});
    EXPECT_FLOAT_EQ(f.at(3, 2), 123.0f);
}

// A variable with no time dimension does not change with time, so it is listed at
// every time the file covers and stays on the same time axis as the rest.
TEST(CfReader, TimeInvariantVariableFollowsTheFileTimes) {
    NcBuilder nc("time_invariant");
    const Axes a = standardAxes(nc);
    nc.put(nc.var("T", NC_FLOAT, {a.time, a.lev, a.lat, a.lon}), digits({2, 3, kNLat, kNLon}));
    nc.put(nc.var("orog", NC_FLOAT, {a.lat, a.lon}), digits({kNLat, kNLon}));
    auto ds = readers::openDataset(nc.finish());

    const auto* orog = ds->catalog().find("orog");
    ASSERT_NE(orog, nullptr);
    EXPECT_EQ(orog->times, ds->catalog().find("T")->times);
    const auto f = ds->readField(core::FieldKey{"orog", kSurface, orog->times.back(), -1});
    EXPECT_FLOAT_EQ(f.at(1, 2), 21.0f);
}

// A variable stored (lon, lat) is lon-major on disk. It must come out in the grid's
// row-major-by-latitude order, not scrambled.
TEST(CfReader, LonLatOrderedVariableIsTransposed) {
    NcBuilder nc("lon_lat");
    const Axes a = standardAxes(nc);
    nc.put(nc.var("swapped", NC_FLOAT, {a.lon, a.lat}), digits({kNLon, kNLat}));  // 10*lon + lat
    auto ds = readers::openDataset(nc.finish());

    const auto* v = ds->catalog().find("swapped");
    ASSERT_NE(v, nullptr);
    const auto f = ds->readField(core::FieldKey{"swapped", kSurface, v->times.front(), -1});
    ASSERT_EQ(f.width(), 4);
    ASSERT_EQ(f.height(), 3);
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(f.at(i, j), 10.0f * i + j) << i << "," << j;
}

// A dimension that is not time, vertical or ensemble cannot be selected, so a
// variable that has one (here a second pressure axis) is left out rather than shown
// as its first slab under the wrong level.
TEST(CfReader, VariableWithAnUnselectableDimensionIsSkipped) {
    NcBuilder nc("extra_dim");
    const Axes a = standardAxes(nc);
    const int lev2 = nc.coord("isobaric1", {300.0, 200.0}, "hPa");
    nc.put(nc.var("T", NC_FLOAT, {a.time, a.lev, a.lat, a.lon}), digits({2, 3, kNLat, kNLon}));
    nc.put(nc.var("w", NC_FLOAT, {a.time, lev2, a.lat, a.lon}), digits({2, 2, kNLat, kNLon}));
    auto ds = readers::openDataset(nc.finish());

    EXPECT_NE(ds->catalog().find("T"), nullptr);
    EXPECT_EQ(ds->catalog().find("w"), nullptr);
}

// ERA5 ensembles carry a `number` dimension. Each member is its own record, and
// reading one returns that member's slab rather than member 0 labelled
// deterministic.
TEST(CfReader, EnsembleDimensionBecomesMembers) {
    NcBuilder nc("ensemble");
    const Axes a = standardAxes(nc);
    const int number = nc.coord("number", {0.0, 1.0, 2.0}, "");
    nc.put(nc.var("t", NC_FLOAT, {number, a.time, a.lat, a.lon}), digits({3, 2, kNLat, kNLon}));
    auto ds = readers::openDataset(nc.finish());

    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->members, (std::vector<int>{0, 1, 2}));
    const auto f = ds->readField(core::FieldKey{"t", kSurface, v->times.back(), 2});
    EXPECT_FLOAT_EQ(f.at(3, 2), 2123.0f);  // member 2, time 1, row 2, column 3
}
