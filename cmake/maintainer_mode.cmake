#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

if (GALERA_MAINTAINER_MODE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()
