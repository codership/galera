#
# Copyright (C) 2020 Codership Oy <info@codership.com>
#

#
# Some older platforms, most notably Ubuntu/Xenial, ship with
# two versions of Check library. One compiled with PIC, another
# without. If found, we prefer check_pic since all of the code
# is compiled with PIC option.
#
find_library(GALERA_HAVE_CHECK_PIC_LIB check_pic)
if (GALERA_HAVE_CHECK_PIC_LIB)
  set(GALERA_UNIT_TEST_LIBS check_pic)
else()
  find_library(GALERA_HAVE_CHECK_LIB check)
  if (NOT GALERA_HAVE_CHECK_LIB)
    message(FATAL_ERROR "Check library not found")
  endif()
  set(GALERA_UNIT_TEST_LIBS "${GALERA_HAVE_CHECK_LIB}")
endif()

find_library(GALERA_HAVE_SUBUNIT_LIB subunit)
if (GALERA_HAVE_SUBUNIT_LIB)
  list(APPEND GALERA_UNIT_TEST_LIBS "${GALERA_HAVE_SUBUNIT_LIB}")
endif()

list(APPEND GALERA_UNIT_TEST_LIBS m)
list(APPEND GALERA_UNIT_TEST_LIBS ${GALERA_SYSTEM_LIBS})

set(REQUIRED_LIBRARIES_TMP ${CMAKE_REQUIRED_LIBRARIES})

list(APPEND CMAKE_REQUIRED_LIBRARIES ${GALERA_UNIT_TEST_LIBS})
check_c_source_compiles("
#include <check.h>
int main() { Suite *s = suite_create(\"test\"); (void)s; return 0; }
 " GALERA_CHECK_COMPILES)
if (NOT GALERA_CHECK_COMPILES)
  message(FATAL_ERROR "Could not compile or link with check library")
endif()

set(CMAKE_REQUIRED_LIBRARIES ${REQUIRED_LIBRARIES_TMP})
