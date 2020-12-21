#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
# OS specific tweaks and libraries.
#

find_library(PTHREAD_LIB pthread)
find_library(RT_LIB rt)
set(GALERA_SYSTEM_LIBS ${PTHREAD_LIB} ${RT_LIB})

message(STATUS "Galera system libs: ${GALERA_SYSTEM_LIBS}")
