cmake_minimum_required(VERSION 3.25)

project(met-viewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

option(MET_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(viewer/core)
add_subdirectory(viewer/readers)
add_subdirectory(viewer/analysis)
add_subdirectory(viewer/render)
add_subdirectory(viewer/app)

if(MET_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install rules + CPack config (installers). Included last so all targets exist.
include(cmake/Packaging.cmake)
