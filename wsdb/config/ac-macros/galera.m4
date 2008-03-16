# Copyright (C) 2007 Codership Oy <info@codership.com>
dnl ---------------------------------------------------------------------------
dnl Macro: WSDB_CHECK_MYSQL_DBUG
dnl Sets HAVE_MYSQL_DBUG if --with-mysql-dbug is used
dnl ---------------------------------------------------------------------------

AC_DEFUN([WSDB_CHECK_MYSQL_DBUG], [
  AC_ARG_WITH([mysql-dbug],
              [
  --without-mysql-dbug        Do not link with mysql dbug library],
              [mysql_dbug="$withval"],
              [mysql_dbug=yes])

  AC_ARG_WITH([mysql-dbug],
              [
  --with-mysql-dbug        Link with mysql dbug library],
              [mysql_dbug="$withval"],
              [mysql_dbug=no])

  AC_MSG_CHECKING([for mysql dbug library])

  have_mysql_dbug=no
  mysql_dbug_includes=
  mysql_dbug_libs=
  case "$mysql_dbug" in
    yes )
      AC_MSG_RESULT([Using mysqld dbug])
      AC_DEFINE([HAVE_MYSQL_DBUG], [1], [Using MySQL dbug library])
      have_mysql_dbug="yes"
      dbug_includes=" -I/usr/local/mysql/include/mysql"
      AC_CHECK_HEADER(my_dbug.h, 
	[dbug_includes=" -I/usr/local/mysql/include/mysql"], [have_mysql_dbug="no"])
      dbug_system_libs=""
      dbug_libs=" /usr/local/mysql/lib/mysql/libdbug.a "


      ;;
    * )
      AC_MSG_RESULT([using original dbug library: $mysql_dbug])
      AC_DEFINE([HAVE_DBUG], [1], [Using original dbug library])
      have_mysql_dbug="no"
      dbug_includes=" -I/usr/local/include/"
      AC_CHECK_HEADER(dbug.h, 
	[dbug_includes=" -I/usr/local/include/"], [have_mysql_dbug="no"])
      dbug_system_libs=""
      dbug_libs=" /usr/local/lib/dbug/libdbug.a "
      ;;
  esac

  AC_SUBST(dbug_includes)
  AC_SUBST(dbug_libs)
  AC_SUBST(dbug_system_libs)
])

dnl ---------------------------------------------------------------------------
dnl END OF MYSQL_CHECK_GALERA SECTION
dnl ---------------------------------------------------------------------------
