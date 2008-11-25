#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#sinclude(config/ac-macros/galera.m4)

AC_PREREQ(2.50)
AC_INIT([libgalera], [0.2.1], [info@codership.com])
AC_CONFIG_SRCDIR([src/galera.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE  # AC_INIT sets version and package name

# Check if debugging information is needed
AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--disable-debug],
              [disable debugging code [[default=enabled]]]),,
              enable_debug="yes")
if test "$enable_debug" != "no"
then
    enable_debug="yes"
    CFLAGS="-g -O0 "
else
    CFLAGS=${CFLAGS:-" -O2 -g "}
    CFLAGS="$CFLAGS -DNDEBUG "
fi
AM_CONDITIONAL(ENABLE_DEBUG, test "$enable_debug" != "no")

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AM_PATH_CHECK()
#PKG_CHECK_MODULES([CHECK], [check >= 0.9.4])

# Checks for libraries.

#if test "$enable_dbug" != no; then
## FIXME: Replace `main' with a function in `-ldbug':
#AC_CHECK_LIB([dbug], [_db_enter_],,
#             AC_MSG_WARN([*** dbug library not found! ***]);
#             enable_dbug="no")
#fi

##AM_CONDITIONAL(ENABLE_DBUG, test "$enable_dbug" != "no")

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
#WSDB_CHECK_MYSQL_DBUG
#AC_SUBST(CPPFLAGS, " ${dbug_includes} ")
#AC_SUBST(LDFLAGS, " ${dbug_libs} ")
#AC_CHECK_HEADERS([limits.h stdint.h stdlib.h string.h my_dbug.h])
AC_CHECK_HEADERS([limits.h stdint.h stdlib.h string.h])
# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset mkdir strerror])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])

CFLAGS="$CFLAGS -std=c99 -Wall -Werror -Wextra -pedantic -Wno-unused-parameter"
AC_OUTPUT
AC_MSG_NOTICE([dbug enabled: $enable_dbug])
AC_MSG_NOTICE(CFLAGS:   $CFLAGS)
AC_MSG_NOTICE(LDFLAGS:  $LDFLAGS)
AC_MSG_NOTICE(CPPFLAGS: $CPPFLAGS)
