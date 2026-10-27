#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "viewer/core/pathutil.h"
#include "viewer/readers/detect.h"
#include "viewer/readers/ireader.h"

using namespace met;

namespace {

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}

// Distinguishes this test process's files from another's running in parallel.
// getpid() did the same, under a name MSVC deprecates.
std::string runToken() {
    static const std::string token = std::to_string(std::random_device{}());
    return token;
}

// A temporary file with exactly these bytes, removed when the test ends.
class TempFile {
public:
    explicit TempFile(const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("met_detect_test_" + runToken() + "_" + std::to_string(counter()) + ".bin");
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

// Windows' narrow file APIs read a path in the ANSI code page, so a reader that
// passes one through them cannot open a file under, say, C:\Users\José or a
// Japanese folder name. Every reader has to take any path the OS can name.
TEST(Detect, OpensEveryFormatFromADirectoryWhoseNameIsNotAscii) {
    const std::string token = runToken();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        std::filesystem::path(u8"met_\u00e9t\u00e9_\u65e5\u672c_" +
                              std::u8string(token.begin(), token.end()));
    std::filesystem::create_directories(dir);
    struct Cleanup {
        std::filesystem::path dir;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } cleanup{dir};

    for (const char* name : {"regular_ll_t500.grib2", "era5_t_pl.nc", "small_latlon.arl"}) {
        const std::filesystem::path copy = dir / name;
        std::filesystem::copy_file(fixture(name), copy);
        // netcdf-c takes the path in the system code page, so a name that does
        // not fit it opens only through its 8.3 short form. A volume with short
        // names disabled has no way to name the file to netcdf-c at all, which
        // is the environment's limit rather than the reader's.
        if (std::string(name).ends_with(".nc") &&
            core::pathForCodePageApi(copy) == core::pathToUtf8(copy)) {
            std::printf("[  NOTE    ] %s: no code-page or 8.3 form of this path; skipped\n", name);
            continue;
        }
        try {
            auto ds = readers::openDataset(copy);
            const auto& v = ds->catalog().variables().front();
            const auto f = ds->readField(core::FieldKey{v.varName, v.levels.front(),
                                                        v.times.front(), v.members.front()});
            EXPECT_GT(f.width(), 0) << name;
        } catch (const std::exception& e) {
            ADD_FAILURE() << name << ": " << e.what();
        }
    }
}

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
    TempFile f(
        "This document describes the GRIB edition 2 format in prose.\n"
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

// openDataset promises ReadError. A directory, or a path the OS cannot stat,
// used to surface as a different exception type from std::filesystem.
TEST(Detect, ADirectoryIsAReadErrorNotAFilesystemError) {
    EXPECT_THROW((void)readers::openDataset(std::filesystem::path(MET_FIXTURE_DIR)),
                 readers::ReadError);
    EXPECT_THROW((void)readers::openDataset(std::filesystem::path(std::string(300, 'x') + "/y")),
                 readers::ReadError);
}
