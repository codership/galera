#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# $Id$
#
AC_PREREQ(2.50)
AC_INIT([libgcs], [0.7.1], [info@codership.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CANONICAL_SYSTEM
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT(/usr/local)

AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)

# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
	      [disable debugging code [[default=enabled]]]),,
	      enable_debug="yes")
if test "$enable_debug" != "no"
then
    CFLAGS="-g -O0 -fno-inline  -DDEBUG_MALLOC -DDEBUG_MUTEX -DDEBUG_ASSERT"
else
    CFLAGS=${CFLAGS:-" -O2 -g "}
    CFLAGS="$CFLAGS -DNDEBUG "
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Check if use of DBUG macros is desired
AC_ARG_ENABLE(dbug,
              AC_HELP_STRING([--disable-dbug],
              [disable Fred Fish[']s DBUG macros [[default=enabled]]]),,
	      enable_dbug="yes")
# Check if dbug location is provided
if test "$enable_dbug" != "no"
then
    enable_dbug="yes"
else
    CFLAGS="$CFLAGS -DGU_DBUG_OFF"
fi

# 
AC_ARG_WITH([galera], [Galera libs location],,with_galera="no")
if test x"$with_galera" != "no"
then
    CFLAGS="$CFLAGS -I$with_galera/include"
    CXXFLAGS="$CXXFLAGS -I$with_galera/include"
    LDFLAGS="$LDFLAGS -L$with_galera/lib"
fi

# Check if Spread backend is desired
AC_ARG_ENABLE(spread,
              AC_HELP_STRING([--disable-spread],
              [disable Spread toolkit backend [[default=enabled]]]),,)
# Check if Spread location is provided
if test "$enable_spread" != "no"
then
    enable_spread="yes"
    AC_ARG_WITH(spread-libs,
                AC_HELP_STRING([--with-spread-libs=PATH],
                [specify where libtspread is installed]),
		[ LDFLAGS="-L$withval $LDFLAGS"])
    AC_ARG_WITH(spread-includes,
                AC_HELP_STRING([--with-spread-includes=PATH],
                [specify where Spread toolkit header is located]),
		[ CFLAGS="$CFLAGS -I$withval"])
fi

AC_ARG_ENABLE([vs],
	AC_HELP_STRING([--disable-vs],
		[disable VS backend [[default=enabled]]]),,)
if test "$enable_vs" != "no"
then
    enable_vs="yes"
fi

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
#AC_PROG_RANLIB
#AC_CHECK_PROG(AR, ar, ar)
AC_PROG_LIBTOOL
AM_PATH_CHECK()
#PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])

# Checks for libraries.
AC_CHECK_LIB([galerautils], [gu_malloc_dbg],,
	     AC_MSG_ERROR([*** galerautils not found! ***]))

# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [pthread_testcancel],,
	     AC_MSG_ERROR([*** POSIX threads not found! ***]))

if test "$enable_spread" != no; then
# FIXME: Replace `main' with a function in `-ltspread':
AC_CHECK_LIB([tspread], [SP_error],,
             AC_MSG_WARN([*** Spread library not found! ***]);
             enable_spread="no")
fi
AM_CONDITIONAL(ENABLE_SPREAD, test "$enable_spread" != "no")
AM_CONDITIONAL(ENABLE_VS, test "$enable_vs" != "no")

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/time.h unistd.h])

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
AC_CHECK_FUNCS([htonl htons ntohl ntohs],,
		AC_MSG_ERROR([*** htonl() and friends not found! ***]))

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 src/Makefile
		 src/unit_tests/Makefile])
		 
CFLAGS="$CFLAGS -Wall -Werror "
AC_OUTPUT

AC_MSG_NOTICE([***])
AC_MSG_NOTICE([Spread backend: $enable_spread])
AC_MSG_NOTICE([***])
AC_MSG_NOTICE([CFLAGS:  $CFLAGS])
AC_MSG_NOTICE([LDFLAGS: $LDFLAGS])
