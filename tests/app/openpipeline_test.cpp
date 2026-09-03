#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include <QStringList>

#include "viewer/app/openpipeline.h"

using namespace met;
using met::app::openBatch;
using met::app::pathsToOpen;

// These functions were private members of MainWindow, so exercising them meant
// constructing the whole widget shell — which is why none of this was covered.
// They are plain functions over plain data now, and this is what that buys.
namespace {

std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}

QStringList qpaths(std::initializer_list<std::filesystem::path> paths) {
    QStringList out;
    for (const auto& p : paths) out << QString::fromStdString(p.string());
    return out;
}

}  // namespace

TEST(PathsToOpen, SortsForDeterministicLoadOrder) {
    // HRRR-style hourly names sort chronologically, and the merged time axis
    // depends on the order files are installed in.
    const auto out = pathsToOpen(qpaths({"/d/f18.arl", "/d/f06.arl", "/d/f12.arl"}), {}, true);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_TRUE(std::is_sorted(out.begin(), out.end()));
    EXPECT_EQ(out.front().filename().string(), "f06.arl");
    EXPECT_EQ(out.back().filename().string(), "f18.arl");
}

TEST(PathsToOpen, DeduplicatesWithinOneRequest) {
    const auto out = pathsToOpen(qpaths({"/d/a.grib2", "/d/a.grib2", "/d/b.grib2"}), {}, true);
    EXPECT_EQ(out.size(), 2u);
}

TEST(PathsToOpen, IgnoresEmptyEntries) {
    QStringList req;
    req << QString() << QStringLiteral("/d/a.grib2") << QString();
    EXPECT_EQ(pathsToOpen(req, {}, true).size(), 1u);
}

// Adding files must not re-open what is already loaded — re-scanning a full HRRR
// day because one file was added is the cost this exists to avoid.
TEST(PathsToOpen, AddSkipsAlreadyLoadedPaths) {
    const std::vector<std::filesystem::path> loaded = {"/d/a.grib2", "/d/b.grib2"};
    const auto out = pathsToOpen(qpaths({"/d/b.grib2", "/d/c.grib2"}), loaded, /*replace=*/false);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().filename().string(), "c.grib2");
}

// A replace starts a fresh set, so the loaded list is irrelevant — including when
// the same path is being reopened deliberately.
TEST(PathsToOpen, ReplaceIgnoresAlreadyLoadedPaths) {
    const std::vector<std::filesystem::path> loaded = {"/d/a.grib2"};
    const auto out = pathsToOpen(qpaths({"/d/a.grib2"}), loaded, /*replace=*/true);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front().filename().string(), "a.grib2");
}

TEST(OpenBatch, OpensReadableFilesAndSamplesTheirGrids) {
    const auto batch = openBatch({fixture("regular_ll_t500.grib2")}, nullptr);
    ASSERT_EQ(batch.opened.size(), 1u);
    EXPECT_TRUE(batch.skipped.isEmpty());
    // The grid is sampled on the worker thread precisely so installBatch does not
    // pay a slab decode on the GUI thread; if that stops happening, this notices.
    ASSERT_EQ(batch.grids.size(), 1u);
    EXPECT_TRUE(batch.grids.front().has_value());
}

// One unreadable file must not lose the batch — the others still open, and the
// skipped entry carries the reason so the user is told why, not just which.
TEST(OpenBatch, SkipsUnreadableFilesWithoutLosingTheRest) {
    const auto batch = openBatch(
        {fixture("regular_ll_t500.grib2"), fixture("definitely_not_here.grib2")}, nullptr);
    EXPECT_EQ(batch.opened.size(), 1u);
    ASSERT_EQ(batch.skipped.size(), 1);
    EXPECT_TRUE(batch.skipped.front().contains("definitely_not_here"));
    EXPECT_TRUE(batch.skipped.front().contains(':')) << "skipped entry should carry a reason";
}

// The progress handle drives the status-bar bar; it must count every file, opened
// or skipped, or a batch with a bad file would stall short of full.
TEST(OpenBatch, CountsEveryFileTowardProgressIncludingFailures) {
    app::JobProgress progress;
    progress.total.store(2, std::memory_order_relaxed);
    (void)openBatch({fixture("regular_ll_t500.grib2"), fixture("definitely_not_here.grib2")},
                    &progress);
    EXPECT_EQ(progress.done.load(std::memory_order_relaxed), 2);
}

TEST(OpenBatch, EmptyRequestYieldsEmptyBatch) {
    const auto batch = openBatch({}, nullptr);
    EXPECT_TRUE(batch.opened.empty());
    EXPECT_TRUE(batch.skipped.isEmpty());
    EXPECT_TRUE(batch.grids.empty());
}
