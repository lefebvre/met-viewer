#include <gtest/gtest.h>

#include <filesystem>

#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MET_FIXTURE_DIR) / name;
}
}  // namespace

// ecCodes decodes GRIB edition 1 and 2 through the same handle API, so the
// reader needs no edition-specific code. Verify the GRIB1 fixture decodes to the
// same known analytic field as its GRIB2 twin.
TEST(Grib1, DecodesLikeGrib2) {
    auto ds = readers::openDataset(fixture("regular_ll_t500.grib1"));
    EXPECT_EQ(ds->formatName(), "GRIB");
    const auto* v = ds->catalog().find("t");
    ASSERT_NE(v, nullptr);
    ASSERT_EQ(v->levels.size(), 1u);
    EXPECT_DOUBLE_EQ(v->levels.front().value, 500.0);

    auto f = ds->readField(core::FieldKey{"t", v->levels.front(), v->times.front(), -1});
    ASSERT_EQ(f.width(), 16);
    ASSERT_EQ(f.height(), 8);
    EXPECT_NEAR(f.at(0, 0), 259.15f, 1e-3);
    EXPECT_NEAR(f.at(15, 7), 264.95f, 1e-3);
}
