#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

add_executable(gcs_tests
  gcs_tests.cpp
  gcs_test_utils.cpp
  gcs_fifo_test.cpp
  ../gcs_fifo_lite.cpp
  gcs_sm_test.cpp
  ../gcs_sm.cpp
  gcs_comp_test.cpp
  ../gcs_comp_msg.cpp
  gcs_state_msg_test.cpp
  ../gcs_state_msg.cpp
  gcs_backend_test.cpp
  ../gcs_backend.cpp
  gcs_proto_test.cpp
  ../gcs_act_proto.cpp
  gcs_defrag_test.cpp
  ../gcs_defrag.cpp
  gcs_node_test.cpp
  ../gcs_node.cpp
  gcs_group_test.cpp
  gcs_memb_test.cpp
  ../gcs_group.cpp
  gcs_core_test.cpp
  ../gcs_core.cpp
  ../gcs_dummy.cpp
  ../gcs_msg_type.cpp
  ../gcs.cpp
  ../gcs_params.cpp
  gcs_fc_test.cpp
  ../gcs_fc.cpp
  )

target_compile_definitions(gcs_tests
  PRIVATE
  -DGALERA_LOG_H_ENABLE_CXX
  -DGCS_CORE_TESTING
  -DGCS_DUMMY_TESTING
  )
if (GALERA_NONDETERMINISTIC_TESTS)
  target_compile_definitions(gcs_tests
    PRIVATE
    -DGCS_ALLOW_GH74
    )
endif()

# TODO: Fix.
target_compile_options(gcs_tests
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(gcs_tests
  gcache
  ${GALERA_UNIT_TEST_LIBS}
  )

add_test(
  NAME gcs_tests
  COMMAND gcs_tests
  )
