find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(met_tests
    test_main.cpp
    core/grid_test.cpp
    core/units_test.cpp
    core/catalog_test.cpp
    core/projected_grid_test.cpp
    core/crs_sphere_test.cpp
    core/timeaxis_test.cpp
    render/colormap_test.cpp
    render/contour_test.cpp
    render/tilemath_test.cpp
    render/warp_test.cpp
    analysis/sample_test.cpp
    analysis/wind_test.cpp
    analysis/analysis_tools_test.cpp
    analysis/heights_test.cpp
    analysis/pointprofile_test.cpp
    analysis/profilewriter_test.cpp
    analysis/derived_test.cpp
    readers/grib_reader_test.cpp
    readers/grib1_test.cpp
    readers/netcdf_reader_test.cpp
    readers/detect_test.cpp
    readers/multidataset_test.cpp
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

# The NetCDF reader tests write their own small files through netcdf-c
# (tests/readers/nc_builder.h), so the test target links it directly. Imported
# targets are scoped to the directory that found them, hence the second lookup;
# the fallback mirrors viewer/readers/CMakeLists.txt.
find_package(netCDF CONFIG)
if(netCDF_FOUND)
    target_link_libraries(met_tests PRIVATE netCDF::netcdf)
else()
    find_package(NetCDF COMPONENTS C REQUIRED)
    target_link_libraries(met_tests PRIVATE NetCDF::NetCDF_C)
endif()
# Likewise ecCodes, for the GRIB messages tests/readers/grib_builder.h writes.
find_package(eccodes CONFIG)
if(eccodes_FOUND)
    target_link_libraries(met_tests PRIVATE eccodes)
else()
    find_package(ecCodes COMPONENTS C REQUIRED)
    target_link_libraries(met_tests PRIVATE ecCodes::ecCodes_C)
endif()

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

# PRE_TEST discovery runs the enumeration at ctest time rather than as a
# post-build step. On Windows the test exe needs the Qt/vcpkg DLLs, which are
# only deployed next to it after the build finishes; discovering at build time
# would launch the exe before the DLLs exist and hang (missing-DLL dialog).
#
# Each test gets a timeout, so a hang -- a deadlock, the modal missing-plugin
# dialog cmake/QtDeploy.cmake describes -- fails that test instead of running to
# the CI job's limit. The whole suite takes seconds; five minutes per test
# leaves room for a sanitizer build. Discovery gets more than the 5 s default
# because under TSan the app tests construct a QApplication before listing.
set(MET_TEST_TIMEOUT 300)
gtest_discover_tests(met_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
                     PROPERTIES TIMEOUT ${MET_TEST_TIMEOUT})

# App-layer tests link the met_app library. They need a QApplication and a
# headless platform plugin (minimal), so they are a separate target — met_tests
# stays Qt-free and fast. test_main_app forces QT_QPA_PLATFORM=minimal before
# constructing QApplication so test discovery works headless.
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Gui)
add_executable(met_app_tests
    test_main_app.cpp
    app/axislimits_test.cpp
    app/coalescingcache_test.cpp
    app/exportname_test.cpp
    app/fieldcache_test.cpp
    app/figureexport_test.cpp
    app/hoverreadout_test.cpp
    app/plotview2d_test.cpp
    app/crosssectionview_test.cpp
    app/skewtview_test.cpp
    app/timecontroller_test.cpp
    app/mapview_gpu_test.cpp
    app/mapview_pick_test.cpp
    app/openpipeline_test.cpp
    app/extractions_profile_test.cpp
    app/pointprofilecache_test.cpp
    app/pointprofiledock_test.cpp
)
target_include_directories(met_app_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(met_app_tests PRIVATE met_options met_app GTest::gtest)

# The open-pipeline tests read real fixtures through the reader stack, so this
# target needs the same fixture/ecCodes paths as met_tests.
target_compile_definitions(met_app_tests PRIVATE
    MET_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    MET_ECCODES_DEFINITION_PATH="${_met_vcpkg_installed}/${VCPKG_TARGET_TRIPLET}/share/eccodes/definitions"
    MET_ECCODES_SAMPLES_PATH="${_met_vcpkg_installed}/${VCPKG_TARGET_TRIPLET}/share/eccodes/samples"
)
# Make the headless QT_QPA_PLATFORM=minimal that test_main_app.cpp sets actually
# resolvable, which takes a different mechanism per Qt linkage. Against a static
# Qt (the default Linux triplet) the plugin has to be linked in, which is what
# qt_import_plugins does and where it stops: it has no effect on a dynamic Qt,
# where the plugin stays a separate DLL that Qt loads at runtime and has to find
# beside the executable. That is the Windows triplet's case, and what
# met_deploy_qt_runtime covers -- see cmake/QtDeploy.cmake.
qt_import_plugins(met_app_tests INCLUDE Qt6::QMinimalIntegrationPlugin)
met_deploy_qt_runtime(met_app_tests PLUGINS Qt6::QMinimalIntegrationPlugin)
gtest_discover_tests(met_app_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60
                     PROPERTIES TIMEOUT ${MET_TEST_TIMEOUT})

# Fuzz harnesses are Clang-only and opt-in; see tests/fuzz/CMakeLists.txt.
if(MET_ENABLE_FUZZING)
    add_subdirectory(fuzz)
endif()
