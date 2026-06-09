#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

# Add an option for specifying a custom Asio installation path
set(GALERA_CUSTOM_ASIO_PATH "" CACHE STRING "Path to custom Asio installation")

macro(CHECK_ASIO_VERSION)
  # Store original CMAKE_REQUIRED_INCLUDES value
  set(SAVED_CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES})

  # Add custom Asio path to CMAKE_REQUIRED_INCLUDES if specified
  if(GALERA_CUSTOM_ASIO_PATH)
    list(APPEND CMAKE_REQUIRED_INCLUDES ${GALERA_CUSTOM_ASIO_PATH})
  endif()

  check_cxx_source_compiles(
    "
#include <asio.hpp>

#define XSTR(x) STR(x)
#define STR(x) #x
#pragma message \"Asio version: \" XSTR(ASIO_VERSION)
#if ASIO_VERSION < 101401
#error Included asio version is too old
#endif

int main()
{
    return 0;
}
"
  ASIO_VERSION_OK
  )

  # Restore original CMAKE_REQUIRED_INCLUDES
  set(CMAKE_REQUIRED_INCLUDES ${SAVED_CMAKE_REQUIRED_INCLUDES})
endmacro()

# Print the major.minor.patch of the Asio that will actually be used.
# HEADER_DIR is a directory containing "asio/version.hpp"; when empty the
# default system include paths are searched instead.
function(REPORT_ASIO_VERSION HEADER_DIR)
  if(HEADER_DIR)
    set(version_hpp "${HEADER_DIR}/asio/version.hpp")
  else()
    find_file(GALERA_ASIO_VERSION_HPP asio/version.hpp)
    set(version_hpp "${GALERA_ASIO_VERSION_HPP}")
    unset(GALERA_ASIO_VERSION_HPP CACHE)
  endif()

  if(NOT version_hpp OR NOT EXISTS "${version_hpp}")
    message(STATUS "Asio version: unable to locate asio/version.hpp")
    return()
  endif()

  file(STRINGS "${version_hpp}" version_line
    REGEX "^#define[ \t]+ASIO_VERSION[ \t]+[0-9]+")
  string(REGEX MATCH "[0-9]+" version "${version_line}")
  if(NOT version)
    message(STATUS "Asio version: unable to parse ASIO_VERSION from ${version_hpp}")
    return()
  endif()

  # ASIO_VERSION == major * 100000 + minor * 100 + sub_minor
  math(EXPR major "${version} / 100000")
  math(EXPR minor "(${version} / 100) % 1000")
  math(EXPR sub_minor "${version} % 100")
  message(STATUS "Using Asio version ${major}.${minor}.${sub_minor} (${version_hpp})")
endfunction()

if(GALERA_CUSTOM_ASIO_PATH)
  message(STATUS "Using custom Asio path: ${GALERA_CUSTOM_ASIO_PATH}")
  include_directories(SYSTEM ${GALERA_CUSTOM_ASIO_PATH})
  set(HAVE_SYSTEM_ASIO_HPP TRUE)
else()
  check_include_file_cxx(asio.hpp HAVE_SYSTEM_ASIO_HPP)
endif()

if (HAVE_SYSTEM_ASIO_HPP)
  CHECK_ASIO_VERSION()
  if (ASIO_VERSION_OK)
    add_definitions(-DHAVE_ASIO_HPP)
    REPORT_ASIO_VERSION("${GALERA_CUSTOM_ASIO_PATH}")
  else()
    unset(HAVE_SYSTEM_ASIO_HPP CACHE)
    unset(ASIO_VERSION_OK CACHE)
  endif()
endif()

if(NOT ASIO_VERSION_OK)
  message(STATUS "Using bundled asio")
  REPORT_ASIO_VERSION("${PROJECT_SOURCE_DIR}/asio")
  include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/asio)
endif()

add_definitions(-DHAVE_ASIO_HPP)
