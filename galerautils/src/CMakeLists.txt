#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

set(GALERAUTILS_CRC32C_SOURCES gu_crc32c.c)

if (GALERA_CRC32C_X86_64)
  set(GALERAUTILS_CRC32C_SOURCES ${GALERAUTILS_CRC32C_SOURCES} gu_crc32c_x86.c)
elseif (GALERA_CRC32C_ARM64)
  set(GALERAUTILS_CRC32C_SOURCES ${GALERAUTILS_CRC32C_SOURCES} gu_crc32c_arm64.c)
endif()

add_library(galerautils
  gu_abort.c
  ${GALERAUTILS_CRC32C_SOURCES}
  gu_dbug.c
  gu_fifo.c
  gu_lock_step.c
  gu_log.c
  gu_mem.c
  gu_mmh3.c
  gu_spooky.c
  gu_rand.c
  gu_threads.c
  gu_hexdump.c
  gu_to.c
  gu_utils.c
  gu_uuid.c
  gu_backtrace.c
  gu_limits.c
  gu_time.c
  gu_init.c
  )

# TODO: These should be eventually fixed.
target_compile_options(galerautils
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter)

target_link_libraries(galerautils ${GALERA_SYSTEM_LIBS})

add_library(galerautilsxx
  gu_vlq.cpp
  gu_datetime.cpp
  gu_exception.cpp
  gu_hexdump.cpp
  gu_serialize.cpp
  gu_logger.cpp
  gu_regex.cpp
  gu_string_utils.cpp
  gu_uri.cpp
  gu_buffer.cpp
  gu_utils++.cpp
  gu_config.cpp
  gu_fdesc.cpp
  gu_mmap.cpp
  gu_alloc.cpp
  gu_rset.cpp
  gu_resolver.cpp
  gu_histogram.cpp
  gu_stats.cpp
  gu_asio.cpp
  gu_debug_sync.cpp
  gu_thread.cpp
  gu_uuid.cpp
  )

# TODO: Warnings should be fixed.
target_compile_options(galerautilsxx
  PRIVATE
  -Wno-conversion
  -Wno-unused-parameter)

target_link_libraries(galerautilsxx galerautils ${GALERA_SSL_LIBS})
