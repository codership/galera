#
# Copyright (C) 2020-2023 Codership Oy <info@codership.com>
#
# Common compiler and preprocessor options.
#

message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")

# TODO: Should this be moved into separate module?
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
    "Build type: Debug, RelWithDebInfo, Release" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
if (CMAKE_VERSION VERSION_LESS "3.1")
  set(CMAKE_C_FLAGS "-std=c99 ${CMAKE_C_FLAGS}")
endif()

set(CMAKE_CXX_STANDARD 11)
if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
      CMAKE_COMPILER_VERSION VERSION_LESS "4.8")
    set(CMAKE_CXX_FLAGS "-std=c++0x ${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")
  endif()
endif()

# Everything will be compiled with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#
# Basic warning flags are set here. For more detailed settings for warnings,
# see maintainer_mode.cmake.
#

# Common C/CXX flags
set(CMAKE_COMMON_FLAGS "-Wall -Wextra -g")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_COMMON_FLAGS "${CMAKE_COMMON_FLAGS} -O0")
  # Enable debug sync points
  add_definitions(-DGU_DBUG_ON)
  # To detect STD library misuse with Debug builds.
  add_definitions(-D_GLIBCXX_ASSERTIONS)
else()
  set(CMAKE_COMMON_FLAGS "${CMAKE_COMMON_FLAGS} -O2")
  # Due to liberal use of assert() in some modules, make sure that
  # non-debug builds have -DNDEBUG enabled.
  add_definitions(-DNDEBUG)
endif()

set(CMAKE_C_FLAGS "${CMAKE_COMMON_FLAGS} ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_COMMON_FLAGS} -Woverloaded-virtual ${CMAKE_CXX_FLAGS}")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_definitions(-D_XOPEN_SOURCE=600)
endif()

if (GALERA_GU_DEBUG_MUTEX)
  add_definitions(-DGU_DEBUG_MUTEX)
endif()
