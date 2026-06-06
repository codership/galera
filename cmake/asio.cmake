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
  else()
    unset(HAVE_SYSTEM_ASIO_HPP CACHE)
    unset(ASIO_VERSION_OK CACHE)
  endif()
endif()

if(NOT ASIO_VERSION_OK)
  message(STATUS "Using bundled asio")
  include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/asio)
endif()

add_definitions(-DHAVE_ASIO_HPP)
