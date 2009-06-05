AC_PREREQ(2.57)
AC_INIT([gcomm], [0.0.1], [info@codership.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL


# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdint.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T


# Check for debug
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
              [disable debugging code (default=enabled)]),,
              enable_debug="yes")
if test "$enable_debug" != "no"
then
    CFLAGS="-g -O1"
    CXXFLAGS="-g -O1"
else
    CFLAGS="-g -O3 -DNDEBUG"
    CXXFLAGS="-g -O3 -DNDEBUG"
fi

AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Additional CFLAGS for more strict compile time checking
# TODO: Find out the most wide set of warning flags  
#       which is still useful (eats system header files etc.)
CFLAGS="$CFLAGS -std=c99 -Wall -Werror -Wextra -pedantic -Wno-unused-parameter"
CXXFLAGS="$CXXFLAGS -Wall -Werror -Wextra -pedantic -Wno-long-long -Wno-unused-parameter -Wno-deprecated -fno-rtti"



#AC_ARG_WITH([galera], [Galera libs location],,with_galera="no")
#if test x"$with_galera" != "no"
#then
#    CFLAGS="$CFLAGS -I$with_galera/include"
#    CXXFLAGS="$CXXFLAGS -I$with_galera/include"
#fi

#AC_CHECK_HEADER([gu_byteswap.h],,
#                AC_MSG_ERROR([*** Galerautils not found ***]))

# Checks for libraries.
AC_CHECK_LIB([galerautils], [gu_malloc_dbg],,
             AC_MSG_ERROR([*** galerautils not found! ***]))



AC_CONFIG_FILES([Makefile
                src/Makefile
                test/Makefile])

AC_OUTPUT
AC_MSG_NOTICE([CFLAGS:   $CFLAGS])
AC_MSG_NOTICE([CXXFLAGS: $CXXFLAGS])
AC_MSG_NOTICE([LDFLAGS:  $LDFLAGS])
