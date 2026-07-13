# (C) Copyright 2011- ECMWF.
#
# This software is licensed under the terms of the Apache Licence Version 2.0
# which can be obtained at http://www.apache.org/licenses/LICENSE-2.0.
# In applying this licence, ECMWF does not waive the privileges and immunities
# granted to it by virtue of its status as an intergovernmental organisation nor
# does it submit to any jurisdiction.

# Copyright 2026 Matthew Krupcale - Modified 2026-03-03

# Try to find ecCodes includes and library
#
# This module defines
#
#   - ecCodes_FOUND               - System has ecCodes
#   - ecCodes_VERSION             - the version of ecCodes
#
# Following components are available:
#
#   - C                           - C interface to ecCodes         (eccodes)
#   - Fortran                     - Fortran interface to ecCodes   (eccodes_f90)
#
# For each component the following are defined:
#
#   - ecCodes_<comp>_FOUND        - whether the component is found
#   - ecCodes::ecCodes_<comp>     - target of component to be used with target_link_libraries()
#
# Caveat: The targets might not link properly with static libraries, setting ecCodes_<comp>_EXTRA_LIBRARIES may be required.
#
# The following paths will be searched in order if set in CMake (first priority) or environment (second priority)
#
#   - ecCodes_<comp>_ROOT
#   - ecCodes_<comp>_DIR
#   - ecCodes_<comp>_PATH
#   - The same variables with a ECCODES or eccodes prefix instead of ecCodes
#   - ecCodes_ROOT
#   - ecCodes_DIR
#   - ecCodes_PATH
#   - The same variables with a ECCODES or eccodes prefix instead of ecCodes
#
# The following variables affect the targets and ecCodes*_LIBRARIES variables:
#
#   - ecCodes_<comp>_EXTRA_LIBRARIES   - added to ecCodes::ecCodes_<comp> INTERFACE_LINK_LIBRARIES and ecCodes_<comp>_LIBRARIES
#
# Notes:
#
#   - If no components are defined, only the C component will be searched.
#

list( APPEND _possible_components C Fortran )

## Header names for each component
set( ecCodes_C_INCLUDE_NAME             eccodes.h )
set( ecCodes_Fortran_INCLUDE_NAME       eccodes.mod ECCODES.mod )

## Library names for each component
set( ecCodes_C_LIBRARY_NAME             eccodes )
set( ecCodes_Fortran_LIBRARY_NAME       eccodes_f90 )

## pkgconfig names for each component
set( ecCodes_C_PKGCONFIG_NAME           eccodes )
set( ecCodes_Fortran_PKGCONFIG_NAME     eccodes_f90 )

foreach( _comp ${_possible_components} )
  string( TOUPPER "${_comp}" _COMP )
  set( _arg_${_COMP} ${_comp} )
  set( _name_${_COMP} ${_comp} )
endforeach()

unset( _search_components )
foreach( _comp ${${CMAKE_FIND_PACKAGE_NAME}_FIND_COMPONENTS} )
  string( TOUPPER "${_comp}" _COMP )
  set( _arg_${_COMP} ${_comp} )
  list( APPEND _search_components ${_name_${_COMP}} )
  if( NOT _name_${_COMP} )
    message( FATAL_ERROR "Find${CMAKE_FIND_PACKAGE_NAME}: COMPONENT ${_comp} is not a valid component. Valid components: ${_possible_components}" )
  endif()
endforeach()
if( NOT _search_components )
  set( _search_components C )
endif()

## Search hints for finding include directories and libraries
foreach( _comp IN ITEMS "" "C" "Fortran" )
  set( __comp "_${_comp}" )
  if( NOT _comp )
    set( __comp "" )
  endif()

  set( _search_hints${__comp} )
  foreach( _name IN ITEMS ecCodes ECCODES eccodes )
    foreach( _var IN ITEMS ROOT DIR PATH )
      list( APPEND _search_hints${__comp} ${${_name}${__comp}_${_var}} ENV ${_name}${__comp}_${_var} )
    endforeach()
  endforeach()

  ## Old-school HPC module env variable names
  foreach( _name IN ITEMS ecCodes ECCODES eccodes )
    list(APPEND _search_hints${__comp} ${${_name}${__comp}} ENV ${_name}${__comp})
  endforeach()
endforeach()

## Search for PkgConfig
find_package(PkgConfig QUIET)

set( _found FALSE )
set( _req_vars )
foreach( _comp ${_search_components} )
  list( APPEND _req_vars ecCodes_${_comp}_INCLUDE_DIR ecCodes_${_comp}_LIBRARY )
  string( TOUPPER "${_comp}" _COMP )

  ## Try pkgconfig
  if ( PkgConfig_FOUND )
    pkg_search_module(_ecCodes_${_comp} QUIET ${ecCodes_${_comp}_PKGCONFIG_NAME} IMPORTED_TARGET)
    set( _ecCodes_${_comp}_CMAKE_TARGET "PkgConfig::_ecCodes_${_comp}" )
    if ( NOT ecCodes_VERSION )
      set( ecCodes_VERSION ${_ecCodes_${_comp}_VERSION} )
    endif ()
  endif ()
  if ( _ecCodes_${_comp}_FOUND )
    set( ${CMAKE_FIND_PACKAGE_NAME}_${_arg_${_COMP}}_FOUND TRUE )
    set( _found TRUE )
    mark_as_advanced(_ecCodes_${_comp}_CMAKE_TARGET)
    list( GET _ecCodes_${_comp}_INCLUDE_DIRS 0 ecCodes_${_comp}_INCLUDE_DIR )
    mark_as_advanced(_ecCodes_${_comp}_INCLUDE_DIRS ecCodes_${_comp}_INCLUDE_DIR)
    get_target_property(_ecCodes_${_comp}_LIBRARIES ${_ecCodes_${_comp}_CMAKE_TARGET} INTERFACE_LINK_LIBRARIES)
    list( GET _ecCodes_${_comp}_LIBRARIES 0 ecCodes_${_comp}_LIBRARY )
    mark_as_advanced(_ecCodes_${_comp}_LIBRARIES ecCodes_${_comp}_LIBRARY)
    if ( NOT TARGET ecCodes::ecCodes_${_comp} )
      add_library(ecCodes::ecCodes_${_comp} INTERFACE IMPORTED)
      set_target_properties(ecCodes::ecCodes_${_comp} PROPERTIES
        INTERFACE_LINK_LIBRARIES ${_ecCodes_${_comp}_CMAKE_TARGET})
      if( DEFINED ecCodes_${_comp}_EXTRA_LIBRARIES )
        target_link_libraries(ecCodes::ecCodes_${_comp} INTERFACE ${ecCodes_${_comp}_EXTRA_LIBRARIES})
      endif ()
    endif ()
    continue ()
  endif ()

  ## Find include directories
  find_path(ecCodes_${_comp}_INCLUDE_DIR
    NAMES ${ecCodes_${_comp}_INCLUDE_NAME}
    DOC "ecCodes ${_comp} include directory"
    HINTS ${_search_hints_${_comp}} ${_search_hints}
    PATH_SUFFIXES include ../../include
  )
  mark_as_advanced(ecCodes_${_comp}_INCLUDE_DIR)

  ## Find libraries for each component
  find_library(ecCodes_${_comp}_LIBRARY
    NAMES ${ecCodes_${_comp}_LIBRARY_NAME}
    DOC "ecCodes ${_comp} library"
    HINTS ${_search_hints_${_comp}} ${_search_hints}
    PATH_SUFFIXES lib ../../lib
  )
  mark_as_advanced(ecCodes_${_comp}_LIBRARY)
  if( ecCodes_${_comp}_LIBRARY AND NOT (ecCodes_${_comp}_LIBRARY MATCHES ".a$") )
    set( ecCodes_${_comp}_LIBRARY_SHARED TRUE )
  endif()
  if( ecCodes_${_comp}_LIBRARY AND ecCodes_${_comp}_INCLUDE_DIR )
    set( ${CMAKE_FIND_PACKAGE_NAME}_${_arg_${_COMP}}_FOUND TRUE )
    set( _found TRUE )

    if (NOT TARGET ecCodes::ecCodes_${_comp})
      add_library(ecCodes::ecCodes_${_comp} UNKNOWN IMPORTED)
      set_target_properties(ecCodes::ecCodes_${_comp} PROPERTIES
        IMPORTED_LOCATION "${ecCodes_${_comp}_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ecCodes_${_comp}_INCLUDE_DIR}")
      if( DEFINED ecCodes_${_comp}_EXTRA_LIBRARIES )
        target_link_libraries(ecCodes::ecCodes_${_comp} INTERFACE ${ecCodes_${_comp}_EXTRA_LIBRARIES})
      endif()
    endif()
  endif()
endforeach()

## Finalize find_package
include(FindPackageHandleStandardArgs)

find_package_handle_standard_args( ${CMAKE_FIND_PACKAGE_NAME}
  REQUIRED_VARS ${_req_vars}
  VERSION_VAR ecCodes_VERSION
  HANDLE_COMPONENTS )

if( ${CMAKE_FIND_PACKAGE_NAME}_FOUND AND NOT ${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY )
  message( STATUS "Find${CMAKE_FIND_PACKAGE_NAME} defines targets:" )
  foreach( _comp ${_search_components} )
    string( TOUPPER "${_comp}" _COMP )

    if( ${CMAKE_FIND_PACKAGE_NAME}_${_arg_${_COMP}}_FOUND )
      message( STATUS "  - ecCodes::ecCodes_${_comp} [${ecCodes_${_comp}_LIBRARY}]")
    endif()
  endforeach()
endif()
