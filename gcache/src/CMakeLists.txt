#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# GCache library
#

add_library(gcache
  GCache_seqno.cpp
  gcache_params.cpp
  gcache_page.cpp
  gcache_page_store.cpp
  gcache_rb_store.cpp
  gcache_mem_store.cpp
  GCache_memops.cpp
  GCache.cpp
  )

# TODO: Fix.
target_compile_options(gcache
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter
  )

target_link_libraries(gcache galerautilsxx)

#
# Gcache test
#

add_executable(gcache_test test.cpp)

target_link_libraries(gcache_test gcache pthread rt)

target_compile_options(gcache_test
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter)
