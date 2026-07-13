find_package(PROJ CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_library(met_core STATIC
    crs.cpp
    grid.cpp
    timeaxis.cpp
    catalog.cpp
    units.cpp
)

target_include_directories(met_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_core PUBLIC met_options PRIVATE PROJ::proj fmt::fmt)

# Bake in the PROJ data directory so proj.db is found without PROJ_DATA set.
if(DEFINED VCPKG_INSTALLED_DIR)
    set(_met_proj_data "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj")
elseif (DEFINED VCPKG_TARGET_TRIPLET)
    set(_met_proj_data "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/proj")
endif()
if(DEFINED _met_proj_data)
    target_compile_definitions(met_core PRIVATE MET_PROJ_DATA="${_met_proj_data}")
endif()
