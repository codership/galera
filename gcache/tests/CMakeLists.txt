#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

add_executable(gcache_tests
  gcache_mem_test.cpp
  gcache_page_test.cpp
  gcache_rb_test.cpp
  gcache_tests.cpp
  )

# TODO: Fix
target_compile_options(gcache_tests
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(gcache_tests gcache ${GALERA_UNIT_TEST_LIBS})

add_test(
  NAME gcache_tests
  COMMAND gcache_tests
  )
