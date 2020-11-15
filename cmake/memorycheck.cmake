#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# Tests can be run with Valgrind by `ctest -D ExperimentalMemCheck`.
#
if (GALERA_WITH_VALGRIND)
  find_program(CTEST_MEMORYCHECK_COMMAND valgrind)
  set(MEMORYCHECK_COMMAND_OPTIONS "--error-exitcode=1 --trace-children=yes --leak-check=full")
  add_definitions(-DGALERA_WITH_VALGRIND)
endif()

