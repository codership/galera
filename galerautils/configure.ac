# Copyright (c) 2007 Codership Oy <info@codership.com>
#
# $Id$
#
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.50)
AC_INIT(galerautils, 0.3.4, [info@codership.com])
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
    CFLAGS=${CFLAGS:-"-O1"} # -O1 to detect unitialized vars
    AM_CFLAGS="-g -fno-inline"
    AM_CPPFLAGS="-DDEBUG_ASSERT -D_FORTIFY_SOURCE=1"
else
    CFLAGS=${CFLAGS:-"-O3"}
    AM_CFLAGS="-g"
    AM_CPPFLAGS="-DNDEBUG"
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

CXXFLAGS=${CXXFLAGS:-"$CFLAGS"}

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

AM_CFLAGS="$AM_CFLAGS -Wall -Werror -Wno-unused-parameter -pedantic"
# CXXFLAGS 
#
# - Turn on most of the warnings, pedantic and ansi
AM_CXXFLAGS="$AM_CFLAGS -ansi -Weffc++ -Wold-style-cast -Wconversion"
# However, disable warnings for:
# - long long datatype ([u]int64_t)
# - unused function parameter (however, this should not be needed)
# - some stl headers may contain deprecated features
# - gu_log.h contains some variadic macros
AM_CXXFLAGS="$AM_CXXFLAGS -Wno-long-long -Wno-deprecated"
# Disable RTTI generation as we don't use it anywhere
AM_CXXFLAGS="$AM_CXXFLAGS -fno-rtti"
AM_CFLAGS="$AM_CFLAGS -std=c99"
AM_CPPFLAGS="$AM_CPPFLAGS -D_XOPEN_SOURCE=600"
AM_LDFLAGS="-Wl,--warn-common -Wl,--fatal-warnings"

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CXXFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([CFLAGS:      $CFLAGS])
AC_MSG_NOTICE([AM_CFLAGS:   $AM_CFLAGS])
AC_MSG_NOTICE([CXXFLAGS:    $CXXFLAGS])
AC_MSG_NOTICE([AM_CXXFLAGS: $AM_CXXFLAGS])
AC_MSG_NOTICE([CPPFLAGS:    $CPPFLAGS])
AC_MSG_NOTICE([AM_CPPFLAGS: $AM_CPPFLAGS])
AC_MSG_NOTICE([LDFLAGS:     $LDFLAGS])
AC_MSG_NOTICE([AM_LDFLAGS:  $AM_LDFLAGS])
AC_MSG_NOTICE([LIBS:        $LIBS])
AC_MSG_NOTICE([])
