#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

find_library(HAVE_CHECK_LIB check)
if (NOT HAVE_CHECK_LIB)
  message(FATAL_ERROR "Check library not found")
endif()

find_library(HAVE_SUBUNIT_LIB subunit)
if (NOT HAVE_SUBUNIT_LIB)
  message(STATUS "Subunit library not found")
endif()

set(CHECK_LIBS check)
if (HAVE_SUBUNIT_LIB)
  set(CHECK_LIBS ${CHECK_LIBS} subunit)
endif()

set(GALERA_UNIT_TEST_LIBS ${CHECK_LIBS} m ${GALERA_SSL_LIBS} dl pthread rt)
