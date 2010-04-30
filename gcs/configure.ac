#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#
# $Id$
#
AC_PREREQ(2.50)
AC_INIT([libgcs], [0.12.1], [info@codership.com])
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
#    AM_CPPFLAGS="-DDEBUG_MALLOC -DDEBUG_MUTEX -DDEBUG_ASSERT -D_FORTIFY_SOURCE=1"
    AM_CPPFLAGS="-DDEBUG_MALLOC -DDEBUG_MUTEX -DDEBUG_ASSERT"
else
    AM_CFLAGS="-O3 -g"
    AM_CPPFLAGS="-DNDEBUG"
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
    AM_CPPFLAGS="$AM_CPPFLAGS -DGU_DBUG_ON"
fi

# Check if Spread backend is desired
AC_ARG_WITH(spread,
            AC_HELP_STRING([--with-spread],
            [enable Spread toolkit backend [[default=disabled]]]),,
            with_spread="no")
# Check if Spread location is provided
if test "$with_spread" != "no"
then
    with_spread="yes"
    AC_ARG_WITH(spread-libs,
                AC_HELP_STRING([--with-spread-libs=PATH],
                [specify where libtspread is installed]),
                [ LDFLAGS="-L$withval $LDFLAGS"])
    AC_ARG_WITH(spread-includes,
                AC_HELP_STRING([--with-spread-includes=PATH],
                [specify where Spread toolkit header is located]),
                [ CFLAGS="$CFLAGS -I$withval"])
fi

AC_ARG_ENABLE([gcomm],
              AC_HELP_STRING([--disable-gcomm],
                             [disable GComm backend [[default=enabled]]]
                            ),,
             )
if test "$enable_gcomm" != "no"
then
    enable_gcomm="yes"
fi

AC_ARG_ENABLE([vs],
              AC_HELP_STRING([--enable-vs],
              [enable VS backend [[default=disabled]]]),,enable_vs="no")
if test "$enable_vs" != "no"
then
    enable_vs="yes"
fi

# Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_C_O
#AC_PROG_RANLIB
#AC_CHECK_PROG(AR, ar, ar)
AC_PROG_LIBTOOL
# m4-1.4.13 can no longer handle obsolete AM_PATH_CHECK so we have to switch to
# PKG_CHECK_MODULES. However CentOS-5.0 has an outdated check version, so 
# by checking m4 version we're trying to guess which check macro to use. 
m4_define(m4_version, m4_esyscmd(m4 --version | head -n1 | cut -d \  -f 4))
m4_if(m4_version_compare(m4_version,1.4.10),
      1,
      [PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])],
      [AM_PATH_CHECK()]
     )

# Checks for libraries.
AC_CHECK_LIB([m], [sqrt],,
             AC_MSG_ERROR([*** libm not found! ***]))

AC_CHECK_LIB([pthread], [pthread_testcancel],,
             AC_MSG_ERROR([*** POSIX threads not found! ***]))

AC_CHECK_LIB([galerautils], [gu_malloc_dbg],,
             AC_MSG_ERROR([*** galerautils not found! ***]))

if test "$enable_gcomm" != "no" -o "$enable_vs" != "no"; then
   AC_CHECK_LIB([galerautils++], [main],,
                AC_MSG_ERROR([*** galerautils++ not found! ***]))
fi

if test "$enable_gcomm" != no; then
   AC_CHECK_LIB([gcomm], [main],,
                AC_MSG_WARN([*** libgcomm not found! ***]);
                enable_gcomm="no")
fi

# FIXME: find a way to check for real functions in those libs
#        currently it just tests abilty to link
for vslib in "vspp" "transportpp" "commonpp"
do
    if test "$enable_vs" != no; then
    AC_CHECK_LIB([gcomm$vslib], [main],,
                AC_MSG_WARN([*** libgcomm$vslib not found! ***]);
                enable_vs="no")
    fi
done

if test "$with_spread" != no; then
AC_CHECK_LIB([tspread], [SP_error],,
             AC_MSG_WARN([*** Spread library not found! ***]);
             with_spread="no")
fi

AM_CONDITIONAL(WITH_SPREAD, test "$with_spread" != "no")
AM_CONDITIONAL(ENABLE_GCOMM, test "$enable_gcomm" != "no")
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

# Compiler and linker flags
AM_CFLAGS="$AM_CFLAGS -Wall -Werror -Wextra -Wno-unused-parameter"
# we have to omit -pedantic here to allow zero-sized arrays in structs
AM_CXXFLAGS="$AM_CFLAGS -ansi -Weffc++ -Wold-style-cast -Wconversion \
 -Wno-long-long"
AM_CFLAGS="$AM_CFLAGS -std=gnu99"
AM_CPPFLAGS="$AM_CPPFLAGS -D_XOPEN_SOURCE=600"
AM_LDFLAGS="-Wl,--warn-common -Wl,--fatal-warnings"

AC_SUBST(AM_CFLAGS)
AC_SUBST(AM_CXXFLAGS)
AC_SUBST(AM_CPPFLAGS)
AC_SUBST(AM_LDFLAGS)

AC_OUTPUT

AC_MSG_NOTICE([***])
AC_MSG_NOTICE([Spread backend: $with_spread])
AC_MSG_NOTICE([Gcomm  backend: $enable_gcomm])
AC_MSG_NOTICE([Vsbes  backend: $enable_vs])
AC_MSG_NOTICE([***])
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
