#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#sinclude(config/ac-macros/galera.m4)

AC_PREREQ(2.50)
AC_INIT([libwsdb], [0.2], [info@codership.com])
AC_CONFIG_SRCDIR([src/certification.c])
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
    CFLAGS="-g -O2 -DDEBUG_MALLOC -DDEBUG_MUTEX "
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

# Define alternate Galera libraries location
AC_ARG_WITH([galera], [Galera libs location],,with_galera="no")
if test x"$with_galera" != "no"
then
    CFLAGS="$CFLAGS -I$with_galera/include"
    CXXFLAGS="$CXXFLAGS -I$with_galera/include"
    LDFLAGS="$LDFLAGS -L$with_galera/lib"
fi

dnl # Check if release version of Galera library is required
dnl AC_ARG_ENABLE(release,
dnl              AC_HELP_STRING([--enable-release],
dnl              [enable release build of Galera library (all dependencies linked in) [[default=disabled]]]),
dnl	      AM_CONDITIONAL(ENABLE_RELEASE, test 1),
dnl	      AM_CONDITIONAL(ENABLE_RELEASE, test 0))

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AM_PATH_CHECK

# Checks for libraries.

#if test "$enable_dbug" != no; then
## FIXME: Replace `main' with a function in `-ldbug':
#AC_CHECK_LIB([dbug], [_db_enter_],,
#             AC_MSG_WARN([*** dbug library not found! ***]);
#             enable_dbug="no")
#fi

AC_CHECK_LIB([galerautils], [gu_log],,
             AC_MSG_ERROR([*** libgalerautils not found! ***]);
	    )

AC_CHECK_LIB([gcs], [gcs_open],,
             AC_MSG_ERROR([*** libgcs not found! ***]);
	    )

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
                 test/Makefile
		 doc/Makefile])

CFLAGS="$CFLAGS -Wall -Werror "
AC_OUTPUT
AC_MSG_NOTICE([dbug enabled: $enable_dbug])
AC_MSG_NOTICE(CFLAGS:   $CFLAGS)
AC_MSG_NOTICE(LDFLAGS:  $LDFLAGS)
AC_MSG_NOTICE(CPPFLAGS: $CPPFLAGS)
