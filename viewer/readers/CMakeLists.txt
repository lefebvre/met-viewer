find_package(eccodes CONFIG REQUIRED)
find_package(netCDF CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

add_library(met_readers STATIC
    detect.cpp
    grib/gribreader.cpp
    grib/gribdataset.cpp
    netcdf/cfreader.cpp
    netcdf/cfdataset.cpp
    arl/arlreader.cpp
    arl/arldataset.cpp
)

target_include_directories(met_readers PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_readers
    PUBLIC met_options met_core
    PRIVATE eccodes netCDF::netcdf fmt::fmt
)
