#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "viewer/core/catalog.h"
#include "viewer/core/field.h"
#include "viewer/core/grid.h"
#include "viewer/core/timeaxis.h"
#include "viewer/readers/detect.h"
#include "viewer/readers/ireader.h"
#include "viewer/readers/multidataset.h"

using namespace met;

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}
core::VerticalLevel hPa(double v) { return {core::VerticalLevel::Type::PressureHPa, v}; }

// One record to seed a fake dataset's catalog.
struct Rec {
    std::string var;
    core::VerticalLevel level;
    core::TimePoint time;
    int member = -1;
};

// A minimal in-memory IDataset. readField() returns a `tag`-sized-and-valued 1-cell
// field so tests can see which source a merged read routed to: values[0] == tag and
// the grid width == tag + 1 (so different sources have distinguishable grids too).
class FakeDataset : public readers::IDataset {
public:
    FakeDataset(int tag, const std::vector<Rec>& recs) : tag_(tag) {
        for (const auto& r : recs)
            catalog_.addRecord(r.var, r.var + " long", "K", "", r.level, r.time, r.member,
                               /*handle=*/0);
        catalog_.finalize();
    }

    const core::DatasetCatalog& catalog() const override { return catalog_; }

    core::Field2D readField(const core::FieldKey& key) override {
        if (!catalog_.resolve(key)) throw readers::ReadError("fake: field not present");
        core::RegularLatLonGrid g;
        g.nlon = tag_ + 1;  // distinct per source
        g.nlat = 1;
        g.dlat = 1;
        g.dlon = 1;
        core::Field2D f;
        f.grid = g;
        f.values.assign(static_cast<std::size_t>(tag_ + 1), static_cast<float>(tag_));
        f.meta.varName = key.varName;
        f.meta.validTime = key.validTime;
        return f;
    }

    std::string formatName() const override { return "Fake"; }

private:
    int tag_;
    core::DatasetCatalog catalog_;
};

std::shared_ptr<readers::IDataset> fake(int tag, const std::vector<Rec>& recs) {
    return std::make_shared<FakeDataset>(tag, recs);
}

}  // namespace

TEST(MultiDataset, UnionsTimesAndRoutesToOwningSource) {
    // Two files, each one hour of "t" at 500 hPa (the HRRR-per-hour shape).
    auto s0 = fake(0, {{"t", hPa(500), core::TimePoint{100}, -1}});
    auto s1 = fake(1, {{"t", hPa(500), core::TimePoint{200}, -1}});
    readers::MultiDataset md({s0, s1});

    const auto* v = md.catalog().find("t");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->times.size(), 2u);
    EXPECT_EQ(v->times[0].epochSeconds, 100);
    EXPECT_EQ(v->times[1].epochSeconds, 200);

    // Each time routes to the source that holds it (tag encoded in value + width).
    auto f0 = md.readField(core::FieldKey{"t", hPa(500), core::TimePoint{100}, -1});
    EXPECT_FLOAT_EQ(f0.at(0, 0), 0.0f);
    EXPECT_EQ(f0.width(), 1);
    auto f1 = md.readField(core::FieldKey{"t", hPa(500), core::TimePoint{200}, -1});
    EXPECT_FLOAT_EQ(f1.at(0, 0), 1.0f);
    EXPECT_EQ(f1.width(), 2);
}

TEST(MultiDataset, DuplicateCellLastSourceWins) {
    // Same (var, level, time) in both: the last-listed source overrides.
    auto s0 = fake(0, {{"t", hPa(500), core::TimePoint{100}, -1}});
    auto s1 = fake(1, {{"t", hPa(500), core::TimePoint{100}, -1}});
    readers::MultiDataset md({s0, s1});

    const auto* v = md.catalog().find("t");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->times.size(), 1u);  // deduped
    auto f = md.readField(core::FieldKey{"t", hPa(500), core::TimePoint{100}, -1});
    EXPECT_FLOAT_EQ(f.at(0, 0), 1.0f);  // source 1 won
}

TEST(MultiDataset, MissingCellThrows) {
    // s0: t@500 for t=100,200; s1: t@850 for t=100. Cell (850, 200) exists in
    // neither, so the merged axes cross but that slab is absent.
    auto s0 = fake(0, {{"t", hPa(500), core::TimePoint{100}, -1},
                       {"t", hPa(500), core::TimePoint{200}, -1}});
    auto s1 = fake(1, {{"t", hPa(850), core::TimePoint{100}, -1}});
    readers::MultiDataset md({s0, s1});

    const auto* v = md.catalog().find("t");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->levels.size(), 2u);
    EXPECT_EQ(v->times.size(), 2u);

    EXPECT_NO_THROW((void)md.readField(core::FieldKey{"t", hPa(500), core::TimePoint{200}, -1}));
    EXPECT_THROW((void)md.readField(core::FieldKey{"t", hPa(850), core::TimePoint{200}, -1}),
                 readers::ReadError);
}

TEST(MultiDataset, FormatNameReflectsSourceCount) {
    auto s0 = fake(0, {{"t", hPa(500), core::TimePoint{100}, -1}});
    auto s1 = fake(1, {{"t", hPa(500), core::TimePoint{200}, -1}});
    readers::MultiDataset md({s0, s1});
    EXPECT_NE(md.formatName().find("Fake"), std::string::npos);
    EXPECT_NE(md.formatName().find("2"), std::string::npos);
}

TEST(OpenDatasets, SkipsUnreadableAndReturnsSingleUnwrapped) {
    // One good file + one bogus path: the bogus one is skipped, and a lone survivor
    // is returned unwrapped (formatName is the leaf's, not a composite).
    readers::OpenResult r =
        readers::openDatasets({fixture("era5_t_pl.nc"), fixture("does_not_exist.nc")});
    ASSERT_NE(r.dataset, nullptr);
    EXPECT_EQ(r.skipped.size(), 1u);
    EXPECT_EQ(r.dataset->formatName(), "NetCDF/CF");
}

TEST(OpenDatasets, AllUnreadableThrows) {
    EXPECT_THROW((void)readers::openDatasets({fixture("nope_a.nc"), fixture("nope_b.grib2")}),
                 readers::ReadError);
    EXPECT_THROW((void)readers::openDatasets({}), readers::ReadError);
}

TEST(OpenDatasets, MergesRealGribAndNetcdfAcrossReaders) {
    // GRIB (t @ 500 only) + NetCDF (t @ 9 levels, 2 times): the merge unions both
    // and each slab keeps its own reader's grid.
    readers::OpenResult r =
        readers::openDatasets({fixture("regular_ll_t500.grib2"), fixture("era5_t_pl.nc")});
    ASSERT_NE(r.dataset, nullptr);
    EXPECT_TRUE(r.skipped.empty());

    const auto* v = r.dataset->catalog().find("t");
    ASSERT_NE(v, nullptr);
    // era5 supplies 9 pressure levels and 2 times; the union is a superset.
    EXPECT_GE(v->levels.size(), 9u);
    EXPECT_GE(v->times.size(), 2u);

    // 100 hPa exists only in the NetCDF source, so reading it (at one of era5's own
    // times) must route there and yield era5's 16×8 grid — proving per-source grids
    // survive the merge.
    auto era5 = readers::openDataset(fixture("era5_t_pl.nc"));
    const core::TimePoint eraT = era5->catalog().find("t")->times.front();
    auto f = r.dataset->readField(core::FieldKey{"t", hPa(100), eraT, -1});
    EXPECT_EQ(f.width(), 16);
    EXPECT_EQ(f.height(), 8);
}
