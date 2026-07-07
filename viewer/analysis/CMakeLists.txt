add_library(met_analysis STATIC
    sample.cpp
    wind.cpp
    crosssection.cpp
    sounding.cpp
    timeseries.cpp
)

target_include_directories(met_analysis PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_analysis PUBLIC met_options met_core)
