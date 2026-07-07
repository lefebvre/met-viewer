find_package(PROJ CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_library(met_core STATIC
    grid.cpp
    timeaxis.cpp
    catalog.cpp
    units.cpp
)

target_include_directories(met_core PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_core PUBLIC met_options PRIVATE PROJ::proj fmt::fmt)
