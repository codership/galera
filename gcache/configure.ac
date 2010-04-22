# Copyright (C) 2009 Codership Oy <http://www.codership.com>
#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
AC_PREREQ(2.50)
AC_INIT([libgcache], [0.1.0], [info@codership.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CANONICAL_SYSTEM
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/usr/local)

# Prevent configure from guessing default CFLAGS
CFLAGS="$CFLAGS"
CXXFLAGS="$CXXFLAGS"

# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
              [disable debugging code [[default=enabled]]]),,
              enable_debug="yes")
if test "$enable_debug" != "no"
then
    AM_CFLAGS="-O1 -g -fno-inline"
    AM_CPPFLAGS="-D_FORTIFY_SOURCE=1"
else
    AM_CFLAGS="-O3 -g"
    AM_CPPFLAGS="-DNDEBUG"
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Checks for programs.
AC_PROG_AWK
AC_LANG([C++])
AC_PROG_CXX
AC_REQUIRE_CPP
AC_PROG_LIBTOOL

AC_LANG_PUSH([C]) # AM_PATH_CHECK() is broken and doesn't #include <string.h>
# m4-1.4.13 can no longer handle obsolete AM_PATH_CHECK so we have to switch to
# PKG_CHECK_MODULES. However CentOS-5.0 has an outdated check version, so 
# by checking m4 version we're trying to deduce which check macro to use. 
m4_define(m4_version, m4_esyscmd(m4 --version | head -n1 | cut -d \  -f 4))
m4_if(m4_version_compare(m4_version,1.4.10),
      1,
      [PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])],
      [AM_PATH_CHECK()]
     )
AC_LANG_POP([C])

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_testcancel],,
             AC_MSG_ERROR([*** POSIX threads not found! ***]))

AC_CHECK_LIB([galerautils], [gu_malloc_dbg],,
              AC_MSG_ERROR([*** galerautils not found! ***]))

AC_CHECK_LIB([galerautils++], [main],,
              AC_MSG_ERROR([*** galerautils++ not found! ***]))

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/time.h unistd.h endian.h byteswap.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gettimeofday localtime_r memset strdup strerror strrchr strtol])

AC_CONFIG_FILES([Makefile
                 src/Makefile])

AM_CFLAGS="$AM_CFLAGS -Wall -Werror -Wextra -pedantic -Wno-unused-parameter"
AM_CXXFLAGS="$AM_CFLAGS -ansi -Weffc++ -Wold-style-cast -Wconversion"
AM_CXXFLAGS="$AM_CXXFLAGS -fno-rtti -Wno-long-long"
AM_CFLAGS="$AM_CFLAGS -std=c99"
AM_LDFLAGS="-Wl,--warn-common -Wl,--fatal-warnings"

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CXXFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([   CFLAGS:   $CFLAGS])
AC_MSG_NOTICE([AM_CFLAGS:   $AM_CFLAGS])
AC_MSG_NOTICE([   CXXFLAGS: $CXXFLAGS])
AC_MSG_NOTICE([AM_CXXFLAGS: $AM_CXXFLAGS])
AC_MSG_NOTICE([   CPPFLAGS: $CPPFLAGS])
AC_MSG_NOTICE([AM_CPPFLAGS: $AM_CPPFLAGS])
AC_MSG_NOTICE([   LDFLAGS:  $LDFLAGS])
AC_MSG_NOTICE([AM_LDFLAGS:  $AM_LDFLAGS])
AC_MSG_NOTICE([   LIBS:     $LIBS])
AC_MSG_NOTICE([])
