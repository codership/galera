#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

macro(CHECK_ASIO_VERSION)
  check_cxx_source_compiles(
    "
#include <asio.hpp>

#define XSTR(x) STR(x)
#define STR(x) #x
#pragma message \"Asio version: \" XSTR(ASIO_VERSION)
#if ASIO_VERSION < 101008
#error Included asio version is too old
#elif ASIO_VERSION >= 101100
#error Included asio version is too new
#endif

int main()
{
    return 0;
}
"
  ASIO_VERSION_OK
  )
endmacro()

check_include_file_cxx(asio.hpp HAVE_SYSTEM_ASIO_HPP)

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
  include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/asio)
endif()

add_definitions(-DHAVE_ASIO_HPP)
