#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

if (GALERA_WITH_ASAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  add_definitions(-DGALERA_WITH_ASAN)
endif()
