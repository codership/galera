#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#
# OS specific tweaks. 
#

# On BSDs packages are usually installed under /usr/local
if (CMAKE_SYSTEM_NAME MATCHES ".*BSD")
   list(APPEND CMAKE_REQUIRED_INCLUDES "/usr/local/include")
   link_directories(/usr/local/lib)
endif()
