#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

# To produce a coverage report, call cmake with -DGALERA_WITH_COVERAGE=ON,
# run
#
#   make
#   make test
#   make ExperimentalCoverage
#   make coverage_report
#
# The coverage report output will be in directory root index.html
#
if (GALERA_WITH_COVERAGE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
  find_program(GALERA_LCOV_EXE lcov)
  find_program(GALERA_GENHTML_EXE genhtml)
  add_custom_target(coverage_report
    ${GALERA_LCOV_EXE} --base-directory ${CMAKE_CURRENT_SOURCE_DIR} --capture --directory ${CMAKE_CURRENT_BINARY_DIR} --output lcov.info --no-external --quiet
    COMMAND ${GALERA_GENHTML_EXE} --output-directory coverage_report lcov.info)
endif()
