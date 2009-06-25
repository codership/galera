# Copyright (c) 2007 Codership Oy <info@codership.com>
#
# $Id$
#
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.50)
AC_INIT(galerautils, 0.3.1, [info@codership.com])
AC_CONFIG_SRCDIR([src/gu_mutex.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
              [disable debugging code (default=enabled)]),,
              enable_debug="yes")
if test "$enable_debug" != "no"
then
    CFLAGS="-g -O2 -DDEBUG_ASSERT "
    CXXFLAGS="$CXXFLAGS -g -O0 "
else
    CFLAGS=${CFLAGS:-" -O3 -g "}
    CFLAGS="$CFLAGS -DNDEBUG "
    CXXFLAGS="$CXXFLAGS -g -O3 -DNDEBUG "
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
#AC_DISABLE_SHARED
AC_PROG_LIBTOOL
AM_PATH_CHECK()
#PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_testcancel],,
             AC_MSG_ERROR([*** POSIX threads not found! ***]))

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([limits.h stdint.h stdlib.h string.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gettimeofday localtime_r strdup])

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 src/Makefile
		 tests/Makefile])

CFLAGS="$CFLAGS -Wall -Werror "
CXXFLAGS="$CXXFLAGS -Wall -Werror -Wextra -pedantic -Wno-long-long -Wno-unused-parameter -Wno-deprecated -fno-rtti -Wno-variadic-macros"
AC_OUTPUT
