#include <cstdlib>

#include <gtest/gtest.h>

namespace {
// Portable env set: setenv is POSIX; MSVC provides _putenv_s instead.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, /*overwrite=*/1);
#endif
}
}  // namespace

int main(int argc, char** argv) {
    // Point ecCodes at the vcpkg-installed definitions/samples so GRIB decode
    // works regardless of the compiled-in default path. Set before any ecCodes
    // context is created (i.e. before tests run).
#ifdef MET_ECCODES_DEFINITION_PATH
    setEnv("ECCODES_DEFINITION_PATH", MET_ECCODES_DEFINITION_PATH);
#endif
#ifdef MET_ECCODES_SAMPLES_PATH
    setEnv("ECCODES_SAMPLES_PATH", MET_ECCODES_SAMPLES_PATH);
#endif
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
