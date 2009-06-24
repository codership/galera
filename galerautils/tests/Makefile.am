# $Id$

AM_CFLAGS  = -I../src

noinst_PROGRAMS   = gu_tests gu_to_test gu_testspp
gu_tests_SOURCES  =                      \
	gu_tests.c                       \
	gu_mem_test.c   gu_mem_test.h    \
	gu_bswap_test.c gu_bswap_test.h  \
	gu_time_test.c  gu_time_test.h   \
	gu_fifo_test.c  gu_fifo_test.h   \
	gu_uuid_test.c  gu_uuid_test.h   \
	gu_dbug_test.c  gu_dbug_test.h

gu_tests_INCLUDES = @CHECK_CFLAGS@
gu_tests_LDADD    = ../src/libgalerautils.la @CHECK_LIBS@

gu_testspp_SOURCES = gu_tests++.cpp
gu_testspp_CPPFLAGS = -I$(top_srcdir)/src @CHECK_CFLAGS@
gu_testspp_LDADD = \
	$(top_builddir)/src/libgalerautils.la \
	$(top_builddir)/src/libgalerautils++.la @CHECK_LIBS@
gu_testspp_LDFLAGS = -static

gu_to_test_SOURCES = gu_to_test.c
gu_to_test_LDADD   = ../src/libgalerautils.la

TESTS = $(noinst_PROGRAMS)

all: check

#
