AC_PREREQ(2.57)
AC_INIT([gcomm], [0.0.1], [info@codership.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

CPPFLAGS="$CPPFLAGS"
CFLAGS="$CFLAGS"
CXXFLAGS="$CXXFLAGS"
# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

# Check library
AM_PATH_CHECK()

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h string.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
#AC_TYPE_UINT16_T
#AC_TYPE_UINT32_T
#AC_TYPE_UINT64_T
#AC_TYPE_UINT8_T

AC_MSG_NOTICE([CXXFLAGS: $CXXFLAGS])

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

# Checks for libraries.
AC_CHECK_LIB([galerautils], [gu_malloc_dbg],,
             AC_MSG_ERROR([*** galerautils not found! ***]))



AC_CONFIG_FILES([Makefile
                src/Makefile
                test/Makefile])

# C/C++ flags for more strict compile time checking
# TODO: Find out the most wide set of warning flags  
#       which is still useful (eats system header files etc.)
AM_CFLAGS="$AM_CFLAGS -Wall -Werror -Wextra -pedantic -Wno-unused-parameter"

# CXXFLAGS 
#
# - Turn on most of the warnings, pedantic and ansi
# However, disable warnings for:
# - long long datatype ([u]int64_t)
# - unused function parameter (however, this should not be needed)
# - some stl headers may contain deprecated features
# - gu_log.h contains some variadic macros
# Disable RTTI generation as we don't use it anywhere
AM_CXXFLAGS="$AM_CFLAGS -ansi -Weffc++ -Wold-style-cast -Wconversion -Wno-long-long -Wno-deprecated -fno-rtti"
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
