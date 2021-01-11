#
# Copyright (C) 2020 Codership Oy <info@codership.com>
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
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif()

set(CMAKE_CXX_STANDARD 98)

# Everything will be compiled with -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#
# Basic warning flags are set here. For more detailed settings for warnings,
# see maintainer_mode.cmake.
#

# C flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_XOPEN_SOURCE=600")
endif()
# CXX flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Woverloaded-virtual -g")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  # To detect STD library misuse with Debug builds.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_ASSERTIONS")
  # Enable debug sync points
  add_definitions(-DGU_DBUG_ON)
else()
  # Due to liberal use of assert() in some modules, make sure that
  # non-debug builds have -DNDEBUG enabled.
  add_definitions(-DNDEBUG)
endif()

if (GALERA_GU_DEBUG_MUTEX)
  add_definitions(-DGU_DEBUG_MUTEX)
endif()
