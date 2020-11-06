#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

message(STATUS "Checking for hardware CRC support for ${CMAKE_SYSTEM_PROCESSOR}")

if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL x86_64)
  check_c_compiler_flag(-msse4.2 HAVE_CRC_FLAG)
  if (HAVE_CRC_FLAG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4.2")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
  endif()
  set(GALERA_CRC32C_X86_64 ON)
elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL aarch64)
  check_c_compiler_flag(-march=armv8-a+crc HAVE_CRC_FLAG)
  if (HAVE_CRC_FLAG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crc")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a+crc")
  endif()
  set(GALERA_CRC32C_ARM64 ON)
endif()

if (NOT HAVE_CRC_FLAG)
  message(STATUS "No hardware CRC support")
  add_definitions(-DGU_CRC32C_NO_HARDWARE)
endif()
