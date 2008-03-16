#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(wsdb_test.c, 1.0, BUG-REPORT-ADDRESS)
AC_CONFIG_SRCDIR([test_cache.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strerror])

AM_PATH_CHECK()
AM_INIT_AUTOMAKE # Calling with version argument is obsolete
AC_OUTPUT
