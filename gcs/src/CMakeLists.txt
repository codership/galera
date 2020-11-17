#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

set(GCS_SOURCES
  gcs_act_cchange.cpp
  gcs_code_msg.cpp
  gcs_params.cpp
  gcs_conf.cpp
  gcs_fifo_lite.cpp
  gcs_msg_type.cpp
  gcs_comp_msg.cpp
  gcs_sm.cpp
  gcs_backend.cpp
  gcs_dummy.cpp
  gcs_act_proto.cpp
  gcs_defrag.cpp
  gcs_state_msg.cpp
  gcs_node.cpp
  gcs_group.cpp
  gcs_core.cpp
  gcs_fc.cpp
  gcs.cpp
  gcs_gcomm.cpp
  )

#
# Gcs library.
#

add_library(gcs ${GCS_SOURCES})

target_compile_definitions(gcs
  PRIVATE
  -DGALERA_LOG_H_ENABLE_CXX
  -DGCS_USE_GCOMM)

if (GALERA_GCS_SM_DEBUG)
  target_compile_definitions(gcs PRIVATE -DGCS_SM_DEBUG)
endif()


# TODO: Fix.
target_compile_options(gcs
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  -Wno-overloaded-virtual
  )

target_link_libraries(gcs gcomm gcache)

#
# Gcs library for Garb daemon.
#

add_library(gcs4garb ${GCS_SOURCES})

target_compile_definitions(gcs4garb
  PRIVATE
  -DGCS_FOR_GARB
  -DGALERA_LOG_H_ENABLE_CXX
  -DGCS_USE_GCOMM
  )

# TODO: Fix.
target_compile_options(gcs4garb
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  -Wno-overloaded-virtual
  )

target_link_libraries(gcs4garb gcomm gcache)

#
# Gcs test program, must be run manually.
#

add_executable(gcs_test gcs_test.cpp)

target_compile_options(gcs_test
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter)

target_link_libraries(gcs_test gcs gcomm)

add_subdirectory(unit_tests)

