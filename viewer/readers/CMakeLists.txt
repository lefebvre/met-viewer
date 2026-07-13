find_package(eccodes CONFIG)
if (eccodes_FOUND)
  set(ECCODES_TARGETS eccodes)
else ()
  # fall back to module
  find_package(ecCodes COMPONENTS C REQUIRED)
  set(ECCODES_TARGETS ecCodes::ecCodes_C)
endif ()

find_package(netCDF CONFIG)
if (netCDF_FOUND)
  set(NETCDF_TARGETS netCDF::netcdf)
else ()
  # fall back to module
  find_package(NetCDF COMPONENTS CXX)
  set(NETCDF_TARGETS NetCDF::NetCDF_CXX)
endif ()

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
    PRIVATE
    ${ECCODES_TARGETS}
    ${NETCDF_TARGETS}
    fmt::fmt
)
