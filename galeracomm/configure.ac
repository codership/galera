AC_PREREQ(2.57)
AC_INIT([libgaleracomm], [0.0.1], [info@codership.com])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

# Prevent configure from guessing default CFLAGS
CFLAGS="$CFLAGS"
CXXFLAGS="$CXXFLAGS"

# Check for debug and set CFLAGS if unset by user
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
              [disable debugging code (default=enabled)]),,
              enable_debug="yes")
if test "$enable_debug" != "no"
then
    AM_CFLAGS="-O1 -g -fno-inline"
    AM_CPPFLAGS="-DDEBUG_ASSERT -D_FORTIFY_SOURCE=1"
else
    AM_CFLAGS="-O3 -g"
    AM_CPPFLAGS="-DNDEBUG"
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

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
# m4-1.4.13 can no longer handle obsolete AM_PATH_CHECK so we have to switch to
# PKG_CHECK_MODULES. However CentOS-5.0 has an outdated check version, so 
# by checking m4 version we're trying to deduce which check macro to use. 
m4_define(m4_version, m4_esyscmd(m4 --version | head -n1 | cut -d \  -f 4))
m4_if(m4_version_compare(m4_version,1.4.10),
      1,
      [PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])],
      [AM_PATH_CHECK()]
     )

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
#AC_TYPE_UINT16_T
#AC_TYPE_UINT32_T
#AC_TYPE_UINT64_T
#AC_TYPE_UINT8_T

AC_CHECK_HEADER([gu_byteswap.h],,
                AC_MSG_ERROR([*** Galerautils not found ***]))

AC_CONFIG_FILES([Makefile
                 common/Makefile
                 common/include/Makefile
                 common/src/Makefile
                 common/test/Makefile
                 transport/Makefile
                 transport/include/Makefile
                 transport/src/Makefile
                 transport/test/Makefile
                 vs/Makefile
                 vs/include/Makefile
                 vs/src/Makefile
                 vs/test/Makefile])

# Additional CFLAGS for more strict compile time checking
# TODO: Find out the most wide set of warning flags  
#       which is still useful (eats system header files etc.)
AM_CFLAGS="$AM_CFLAGS -Wall -Werror -Wextra -pedantic -Wno-unused-parameter -fno-strict-aliasing"
AM_CXXFLAGS="$AM_CFLAGS -ansi -Weffc++ -Wold-style-cast -Wconversion -Wno-long-long -fno-rtti"
AM_CFLAGS="$AM_CFLAGS -std=c99"
AM_LDFLAGS="$AM_LDFLAGS -Wl,--warn-common -Wl,--fatal-warnings" 
 
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
