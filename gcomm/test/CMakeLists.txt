#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

add_executable(check_gcomm
  check_fair_send_queue.cpp
  check_gcomm.cpp
  check_trace.cpp
  check_types.cpp
  check_util.cpp
  check_evs2.cpp
  check_pc.cpp
  )

target_link_libraries(check_gcomm
  gcomm
  ${GALERA_UNIT_TEST_LIBS}
  )

# TODO: Fix
target_compile_options(check_gcomm
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  -Wno-overloaded-virtual
  )

add_test(
  NAME check_gcomm
  COMMAND check_gcomm
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Testing
  )

#
# Nondeterministic unit tests, must be run manually.
#

add_executable(check_gcomm_nondet
  check_gcomm_nondet.cpp
  check_gmcast.cpp
  check_pc_nondet.cpp
  check_util_nondet.cpp)

target_compile_options(check_gcomm_nondet
  PRIVATE
  -Wno-conversion
  -Wno-overloaded-virtual
  -Wno-unused-parameter
  )

target_link_libraries(check_gcomm_nondet gcomm ${GALERA_UNIT_TEST_LIBS})

#
# Old SSL test, must be run manually.
#

add_executable(ssl_test ssl_test.cpp)

target_compile_options(ssl_test
  PRIVATE
  -Wno-unused-parameter)

target_link_libraries(ssl_test gcomm)

