#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

add_executable(galera_check
  galera_check.cpp
  data_set_check.cpp
  key_set_check.cpp
  write_set_ng_check.cpp
  write_set_check.cpp
  trx_handle_check.cpp
  service_thd_check.cpp
  ist_check.cpp
  saved_state_check.cpp
  defaults_check.cpp
  )

target_include_directories(galera_check
  PRIVATE
  ${CMAKE_SOURCE_DIR}/galera/src
  ${CMAKE_SOURCE_DIR}/wsrep/src
  )

# TODO: Fix.
target_compile_options(galera_check
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(galera_check galera_smm_static ${GALERA_UNIT_TEST_LIBS})

add_test(
  NAME galera_check
  COMMAND galera_check
  )
