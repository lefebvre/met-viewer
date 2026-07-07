find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(met_tests
    core/smoke_test.cpp
)

target_include_directories(met_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(met_tests PRIVATE
    met_options
    met_core
    met_readers
    GTest::gtest
    GTest::gtest_main
)

gtest_discover_tests(met_tests)
