#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

message(STATUS "CMake version ${CMAKE_VERSION}")
cmake_minimum_required(VERSION 2.8)
project(galera-3)

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckCXXCompilerFlag)

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/common
  ${CMAKE_SOURCE_DIR}/galerautils/src
  ${CMAKE_SOURCE_DIR}/gcomm/src
  ${CMAKE_SOURCE_DIR}/gcache/src
  ${CMAKE_SOURCE_DIR}/gcs/src
  ${CMAKE_SOURCE_DIR}/wsrep/src
  )

# Options to control compiler options and error behavior.
option(GALERA_MAINTAINER_MODE "Fail compilation on warnings" OFF)

# Galera library options.
option(GALERA_WITH_SSL "Compile Galera with SSL" ON)
option(GALERA_VERSION_SCRIPT "Limit symbols visible from Galera DSO" ON)
option(GALERA_STATIC "Build statically linked binaries" OFF)
option(GALERA_SOURCE
  "Configure for source package only, skipping build configuration" OFF)

# Developer options and instrumentation.
option(GALERA_WITH_ASAN "Enable ASAN instrumentation" OFF)
option(GALERA_WITH_VALGRIND "Optionally run tests with valgrind" OFF)
option(GALERA_WITH_COVERAGE "Compile with coverage instrumentation" OFF)
option(GALERA_GCS_SM_DEBUG
  "Enable dumping of send monitor state and history" OFF)
option(GALERA_GU_DEBUG_MUTEX "Enable mutex debug instrumentation" OFF)
option(GALERA_GU_DBUG_ON "Enable sync point macros (ON for Debug builds)" OFF)

#
# Set cmake policies before doing any checks.
#

# In CMake 3.12 and above, the
#
# * ``check_include_file`` macro in the ``CheckIncludeFile`` module, the
# * ``check_include_file_cxx`` macro in the
#   ``CheckIncludeFileCXX`` module, and the
# * ``check_include_files`` macro in the ``CheckIncludeFiles`` module
#
# now prefer to link the check executable to the libraries listed in the
# ``CMAKE_REQUIRED_LIBRARIES`` variable.  This policy provides compatibility
# with projects that have not been updated to expect this behavior.
if (POLICY CMP0075)
  CMAKE_POLICY(SET CMP0075 OLD)
endif()

# Determine version first to have it near the top of build log.
include(cmake/version.cmake)
include(cmake/package.cmake)

# Do not execute other configuration steps if building source only.
if (GALERA_SOURCE)
  return()
endif()

# Source compiler/OS settings first as it sets used C/C++ standards,
# common compiler flags and include/link paths.
include(cmake/compiler.cmake)
include(cmake/os.cmake)
# Instrumentation.
include(cmake/asan.cmake)

# Common definitions for all modues.
include(cmake/common.cmake)

# Libraries, language library features.
include(cmake/ssl.cmake)
include(cmake/asio.cmake)
include(cmake/array.cmake)
include(cmake/boost.cmake)
include(cmake/crc32c.cmake)
include(cmake/endian.cmake)
include(cmake/shared_ptr.cmake)
include(cmake/unordered.cmake)
include(cmake/check.cmake)
include(cmake/memorycheck.cmake)
include(cmake/coverage.cmake)
# Keep the maintainer mode as last to avoid feature checks failing
# in compiler warnings.
include(cmake/maintainer_mode.cmake)

include(CTest)
enable_testing()
add_subdirectory(galerautils)
add_subdirectory(gcomm)
add_subdirectory(gcache)
add_subdirectory(gcs)
add_subdirectory(garb)
add_subdirectory(galera)
add_subdirectory(scripts/packages)
add_subdirectory(wsrep/tests)

install(FILES
  ${CMAKE_SOURCE_DIR}/AUTHORS
  ${CMAKE_SOURCE_DIR}/COPYING
  ${CMAKE_SOURCE_DIR}/README
  DESTINATION doc)
install(FILES ${CMAKE_SOURCE_DIR}/asio/LICENSE_1_0.txt
  DESTINATION doc
  RENAME LICENSE.asio)
install(FILES ${CMAKE_SOURCE_DIR}/chromium/LICENSE
  DESTINATION doc
  RENAME LICENSE.chromium)

message(STATUS "")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
get_directory_property(DirDefs COMPILE_DEFINITIONS)
message(STATUS "COMPILE_DEFINITIONS: ${DirDefs}")
message(STATUS "CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "GALERA_SYSTEM_LIBS: ${GALERA_SYSTEM_LIBS}")
message(STATUS "GALERA_UNIT_TEST_LIBS: ${GALERA_UNIT_TEST_LIBS}")
message(STATUS "")
