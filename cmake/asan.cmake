#
# Copyright (C) 2020-2023 Codership Oy <info@codership.com>
#

if (GALERA_WITH_ASAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  add_definitions(-DGALERA_WITH_ASAN)
endif()

if (GALERA_WITH_UBSAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
  add_definitions(-DGALERA_WITH_UBSAN)
  # don't run unit tests that use outdaed unaligned record set format
  add_definitions(-DGALERA_ONLY_ALIGNED)

  find_library(UBSAN_LIB NAMES ubsan libubsan.so.1)
  message(STATUS ${UBSAN_LIB})
  set(CMAKE_REQUIRED_LIBRARIES ${UBSAN_LIB})
  check_c_source_compiles("int main() { return 0; }" GALERA_HAVE_UBSAN_LIB)
  if (NOT GALERA_HAVE_UBSAN_LIB)
    message(FATAL_ERROR "Could not find UBSAN support library")
  endif()
  unset(CMAKE_REQUIRED_LIBRARIES)
  list(APPEND GALERA_SYSTEM_LIBS ${UBSAN_LIB})
endif()
