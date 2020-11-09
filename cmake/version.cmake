#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

file(READ "${CMAKE_SOURCE_DIR}/GALERA_VERSION" ver)
string(REGEX MATCH "GALERA_VERSION_WSREP_API=([0-9]*)" _ ${ver})
set(GALERA_VERSION_WSREP_API ${CMAKE_MATCH_1})
string(REGEX MATCH "GALERA_VERSION_MAJOR=([0-9]*)" _ ${ver})
set(GALERA_VERSION_MAJOR ${CMAKE_MATCH_1})
string(REGEX MATCH "GALERA_VERSION_MINOR=([0-9]*)" _ ${ver})
set(GALERA_VERSION_MINOR ${CMAKE_MATCH_1})
string(REGEX MATCH "GALERA_VERSION_EXTRA=([0-9a-zA-Z]*)" _ ${ver})
set(GALERA_VERSION_EXTRA ${CMAKE_MATCH_1})

set(GALERA_VERSION
  "${GALERA_VERSION_MAJOR}.${GALERA_VERSION_MINOR}${GALERA_VERSION_EXTRA}")

#
# Determine Galera build revision in order of:
# - Given from commandline
# - Try to read git revision
#
# If the GALERA_REVISION is not explicitly given from command line or
# is empty string, git revision is written into GALERA_REVISION file but not
# stored into GALERA_REVISION variable. This is to allow caller to
# explicitly specify the revision for package name (see package.cmake).
#
if (NOT GALERA_REVISION)
  execute_process(
    COMMAND git rev-parse --short HEAD
    RESULT_VARIABLE GIT_GALERA_REVISION_RESULT
    OUTPUT_VARIABLE var)
  if (NOT GIT_GALERA_REVISION_RESULT)
    string(STRIP ${var} GALERA_GIT_REVISION)
    file(WRITE ${CMAKE_BINARY_DIR}/GALERA_REVISION ${GALERA_GIT_REVISION})
  else()
    file(WRITE ${CMAKE_BINARY_DIR}/GALERA_REVISION "XXXX")
  endif()
else()
  file(WRITE ${CMAKE_BINARY_DIR}/GALERA_REVISION ${GALERA_REVISION})
endif()

message(STATUS
  "Building Galera version ${GALERA_VERSION} revision ${GALERA_REVISION} wsrep-API version ${GALERA_VERSION_WSREP_API}")
