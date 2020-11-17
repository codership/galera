#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

macro(GALERA_SET_C_WARNING warn)
  check_c_compiler_flag(-${warn} GALERA_HAVE_${warn})
  if (GALERA_HAVE_${warn})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -${warn}")
  endif()
endmacro()

macro(GALERA_SET_CXX_WARNING warn)
  check_cxx_compiler_flag(-${warn} GALERA_HAVE_${warn})
  if (GALERA_HAVE_${warn})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -${warn}")
  endif()
endmacro()

if (GALERA_MAINTAINER_MODE)
  # Old C produces lots of warnings about conversions, skip
  # conversion warnings until the code is fixed.
  GALERA_SET_C_WARNING(Wno-conversion)
  GALERA_SET_C_WARNING(Wno-sign-conversion)
  GALERA_SET_C_WARNING(Wself-assign)

  GALERA_SET_CXX_WARNING(Wconversion)
  # Too many warnings about sign conversions, keep them disabled
  # until fixed.
  GALERA_SET_CXX_WARNING(Wno-sign-conversion)
  GALERA_SET_CXX_WARNING(Wnon-virtual-dtor)
  GALERA_SET_CXX_WARNING(Wmissing-field-initializers)
  GALERA_SET_CXX_WARNING(Wself-assign)

  # Turn warnings as errors
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()
