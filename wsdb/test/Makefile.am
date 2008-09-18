# Copyright (C) 2007 Codership Oy <info@codership.com>
TESTS=check_wsdb
noinst_PROGRAMS=check_wsdb
check_wsdb_SOURCES=    \
	test_file.c    \
	test_hash.c    \
	test_cache.c   \
	test_wsdb.c    \
	test_version_file.c \
	test_xdr.c     \
	test_log.c     \
	test_mempool.c \
	wsdb_test.c    \
	wsdb_test.h

check_wsdb_INCLUDES= @CHECK_CFLAGS@
check_wsdb_LDADD= ../src/libwsdb.la @CHECK_LIBS@ 
