find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(met_tests
    test_main.cpp
    core/grid_test.cpp
    core/units_test.cpp
    core/catalog_test.cpp
    core/projected_grid_test.cpp
    render/colormap_test.cpp
    render/contour_test.cpp
    render/tilemath_test.cpp
    render/warp_test.cpp
    analysis/sample_test.cpp
    analysis/wind_test.cpp
    analysis/analysis_tools_test.cpp
    readers/grib_reader_test.cpp
    readers/grib1_test.cpp
    readers/netcdf_reader_test.cpp
    readers/lambert_test.cpp
    readers/arl_test.cpp
)

target_include_directories(met_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(met_tests PRIVATE
    met_options
    met_core
    met_readers
    met_render
    met_analysis
    GTest::gtest
)

# Resolve the vcpkg install dir (a preset may relocate it out of the build tree).
if(DEFINED VCPKG_INSTALLED_DIR)
    set(_met_vcpkg_installed "${VCPKG_INSTALLED_DIR}")
else()
    set(_met_vcpkg_installed "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()

# Locations resolved at configure time so tests run from any working directory.
target_compile_definitions(met_tests PRIVATE
    MET_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    MET_ECCODES_DEFINITION_PATH="${_met_vcpkg_installed}/${VCPKG_TARGET_TRIPLET}/share/eccodes/definitions"
    MET_ECCODES_SAMPLES_PATH="${_met_vcpkg_installed}/${VCPKG_TARGET_TRIPLET}/share/eccodes/samples"
)

gtest_discover_tests(met_tests)
