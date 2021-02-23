#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
# OS specific tweaks and libraries.
#

find_library(PTHREAD_LIB pthread)
find_library(RT_LIB rt)
set(GALERA_SYSTEM_LIBS ${PTHREAD_LIB} ${RT_LIB})

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  # Check if linkage with atomic library is needed for 8 byte atomics
  set(ATOMIC_8_TEST_C_SOURCE
     "int main() { long long val; __atomic_fetch_add_8(&val, 1, __ATOMIC_SEQ_CST); return 0;}")
  check_c_source_compiles("${ATOMIC_8_TEST_C_SOURCE}" GALERA_HAVE_ATOMIC)
  if (NOT GALERA_HAVE_ATOMIC)
    find_library(ATOMIC_LIB NAMES atomic libatomic.so.1)
    message(STATUS ${ATOMIC_LIB})
    set(CMAKE_REQUIRED_LIBRARIES ${ATOMIC_LIB})
    check_c_source_compiles("${ATOMIC_8_TEST_C_SOURCE}" GALERA_HAVE_ATOMIC_LIB)
    if (NOT GALERA_HAVE_ATOMIC_LIB)
      message(FATAL_ERROR "Could not find support for 64 bit atomic operations")
    endif()
    unset(CMAKE_REQUIRED_LIBRARIES)
    list(APPEND GALERA_SYSTEM_LIBS ${ATOMIC_LIB})
  endif()
endif()

message(STATUS "Galera system libs: ${GALERA_SYSTEM_LIBS}")
