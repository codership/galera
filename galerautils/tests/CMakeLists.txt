#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# C galerautils tests.
#

add_executable(gu_tests
  gu_tests.c
  gu_crc32c_test.c
  gu_mem_test.c
  gu_bswap_test.c
  gu_fnv_test.c
  gu_mmh3_test.c
  gu_spooky_test.c
  gu_hash_test.c
  gu_time_test.c
  gu_fifo_test.c
  gu_uuid_test.c
  gu_dbug_test.c
  gu_lock_step_test.c
  gu_str_test.c
  gu_utils_test.c
  )

# TODO: These should be eventually fixed.
# - Wno-unused-parameter
#
# Suppress -Wself-assign which may emit unwanted warnings when
# using byte swapping macros in way like k = gu_le64(k).
#
target_compile_options(gu_tests
  PRIVATE
  -Wno-unused-parameter
  )

target_link_libraries(gu_tests galerautils ${GALERA_UNIT_TEST_LIBS})

add_test(
  NAME gu_tests
  COMMAND gu_tests
  )

#
# C++ galerautils tests.
#

add_executable(gu_tests++
  gu_atomic_test.cpp
  gu_vector_test.cpp
  gu_string_test.cpp
  gu_vlq_test.cpp
  gu_digest_test.cpp
  gu_mem_pool_test.cpp
  gu_alloc_test.cpp
  gu_rset_test.cpp
  gu_string_utils_test.cpp
  gu_uri_test.cpp
  gu_config_test.cpp
  gu_net_test.cpp
  gu_datetime_test.cpp
  gu_histogram_test.cpp
  gu_stats_test.cpp
  gu_thread_test.cpp
  gu_asio_test.cpp
  gu_deqmap_test.cpp
  gu_tests++.cpp
  )

# TODO: These should be eventually fixed.
target_compile_options(gu_tests++
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(gu_tests++
  galerautilsxx
  ${GALERA_UNIT_TEST_LIBS}
  )

add_test(
  NAME gu_tests++
  COMMAND gu_tests++
  )

#
# Deqmap micro benchmark.
#

add_executable(deqmap_bench deqmap_bench.cpp)

target_compile_options(deqmap_bench
  PRIVATE
  -Wno-conversion)

target_link_libraries(deqmap_bench galerautilsxx rt)

#
# CRC32C micro benchmark.
#
add_executable(crc32c_bench crc32c_bench.cpp)

target_compile_options(crc32c_bench
  PRIVATE
  -Wno-conversion)

target_link_libraries(crc32c_bench galerautilsxx)

#
# Hash implementation micro benchmark.
#

add_executable(avalanche avalanche.c)

target_compile_options(avalanche
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter)

target_link_libraries(avalanche galerautils)

#
# Test for old TO monitor implementation, should be removed once all of the
# code which uses gu_to module has been removed.
#
add_executable(gu_to_test gu_to_test.c)

target_link_libraries(gu_to_test galerautils)

target_compile_options(gu_to_test
  PRIVATE
  -Wno-conversion)
