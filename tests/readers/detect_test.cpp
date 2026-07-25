#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "viewer/readers/detect.h"
#include "viewer/readers/ireader.h"

using namespace met;

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}

// A temporary file with exactly these bytes, removed when the test ends.
class TempFile {
public:
    explicit TempFile(const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("met_detect_test_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter()) + ".bin");
        std::ofstream out(path_, std::ios::binary);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    static int counter() {
        static int n = 0;
        return ++n;
    }
    std::filesystem::path path_;
};

}  // namespace

TEST(Detect, RecognizesEachFixtureFormat) {
    EXPECT_EQ(readers::openDataset(fixture("regular_ll_t500.grib2"))->formatName(), "GRIB");
    EXPECT_EQ(readers::openDataset(fixture("regular_ll_t500.grib1"))->formatName(), "GRIB");
    EXPECT_EQ(readers::openDataset(fixture("era5_t_pl.nc"))->formatName(), "NetCDF/CF");
    EXPECT_EQ(readers::openDataset(fixture("small_latlon.arl"))->formatName(), "ARL");
}

// The GRIB probe scans a window for the magic rather than requiring it at offset 0
// (real files carry WMO preambles), so it must not claim a text file that merely
// mentions GRIB — the edition byte is what makes the match trustworthy.
TEST(Detect, TextMentioningGribIsNotClaimed) {
    TempFile f("This document describes the GRIB edition 2 format in prose.\n"
               "Nothing here is actually a GRIB message.\n");
    EXPECT_THROW((void)readers::openDataset(f.path()), readers::ReadError);
}

TEST(Detect, EmptyAndTinyFilesAreRejectedNotCrashed) {
    TempFile empty("");
    EXPECT_THROW((void)readers::openDataset(empty.path()), readers::ReadError);
    TempFile tiny("GRIB");  // shorter than the 8 bytes the probe needs
    EXPECT_THROW((void)readers::openDataset(tiny.path()), readers::ReadError);
}

TEST(Detect, MissingFileThrows) {
    EXPECT_THROW((void)readers::openDataset(fixture("definitely_not_here.grib2")),
                 readers::ReadError);
}

// The ARL probe keys on an INDX label with ASCII date digits. A file with the
// right marker but a garbage header must fail in open(), not be silently accepted
// with a degenerate grid.
TEST(Detect, ArlLikeHeaderWithBadDimensionsIsRejected) {
    std::string s(512, ' ');
    s.replace(0, 12, "240101000000");
    s.replace(12, 2, "AA");
    s.replace(14, 4, "INDX");
    TempFile f(s);  // INDX label, but the grid dimensions parse as zero
    EXPECT_THROW((void)readers::openDataset(f.path()), readers::ReadError);
}
