#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
# OS specific tweaks and libraries.
#

# On BSDs packages are usually installed under /usr/local
if (CMAKE_SYSTEM_NAME MATCHES ".*BSD")
   list(APPEND CMAKE_REQUIRED_INCLUDES "/usr/local/include")
   link_directories(/usr/local/lib)
endif()

find_library(PTHREAD_LIB pthread)
find_library(RT_LIB rt)
set(GALERA_SYSTEM_LIBS ${PTHREAD_LIB} ${RT_LIB})

message(STATUS "Galera system libs: ${GALERA_SYSTEM_LIBS}")
