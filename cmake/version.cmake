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
# - GALERA_REVISION file at source root
# - Try to read git revision
#
if (NOT GALERA_REVISION)
  unset(var)
  if (EXISTS ${CMAKE_SOURCE_DIR}/GALERA_REVISION)
    file(READ ${CMAKE_SOURCE_DIR}/GALERA_REVISION var)
    string(STRIP ${var} GALERA_REVISION)
  endif()
  if (NOT GALERA_REVISION)
    execute_process(
      COMMAND git rev-parse --short HEAD
      RESULT_VARIABLE GIT_GALERA_REVISION_RESULT
      OUTPUT_VARIABLE var)
    if (GIT_GALERA_REVISION_RESULT)
      unset(GALERA_REVISION)
    else()
      string(STRIP ${var} GALERA_REVISION)
    endif()
  endif()
endif()

if (NOT GALERA_REVISION)
  set(GALERA_REVISION "XXXX")
endif()

if (NOT EXISTS ${CMAKE_BINARY_DIR}/GALERA_REVISION)
  file(WRITE ${CMAKE_BINARY_DIR}/GALERA_REVISION ${GALERA_REVISION})
endif()

message(STATUS
  "Building Galera version ${GALERA_VERSION} revision ${GALERA_REVISION} wsrep-API version ${GALERA_VERSION_WSREP_API}")
