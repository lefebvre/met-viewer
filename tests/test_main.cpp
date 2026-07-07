#include <cstdlib>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // Point ecCodes at the vcpkg-installed definitions/samples so GRIB decode
    // works regardless of the compiled-in default path. setenv before any
    // ecCodes context is created (i.e. before tests run).
#ifdef MET_ECCODES_DEFINITION_PATH
    setenv("ECCODES_DEFINITION_PATH", MET_ECCODES_DEFINITION_PATH, /*overwrite=*/1);
#endif
#ifdef MET_ECCODES_SAMPLES_PATH
    setenv("ECCODES_SAMPLES_PATH", MET_ECCODES_SAMPLES_PATH, /*overwrite=*/1);
#endif
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
