# $Id$

AM_CPPFLAGS = @AM_CPPFLAGS@ @CHECK_CFLAGS@ -I$(top_srcdir)/src

#noinst_PROGRAMS   = gu_tests gu_to_test gu_testspp
noinst_PROGRAMS   = gu_tests gu_to_test gu_testspp

gu_tests_SOURCES  = \
	gu_tests.c                              \
	gu_mem_test.c       gu_mem_test.h       \
	gu_bswap_test.c     gu_bswap_test.h     \
	gu_time_test.c      gu_time_test.h      \
	gu_fifo_test.c      gu_fifo_test.h      \
	gu_uuid_test.c      gu_uuid_test.h      \
	gu_dbug_test.c      gu_dbug_test.h      \
	gu_options_test.c   gu_options_test.h   \
	gu_lock_step_test.c gu_lock_step_test.h

gu_tests_LDADD   = ../src/libgalerautils.la @CHECK_LIBS@ -lm
gu_tests_LDFLAGS = @AM_LDFLAGS@ -static

gu_testspp_SOURCES = \
	gu_tests++.cpp  gu_tests++.hpp  \
	gu_string_test.cpp gu_string_test.hpp \
	gu_uri_test.cpp gu_uri_test.hpp \
	gu_net_test.cpp gu_net_test.hpp \
	gu_datetime_test.cpp gu_datetime_test.hpp

gu_testspp_LDADD = \
	$(top_builddir)/src/libgalerautils.la \
	$(top_builddir)/src/libgalerautils++.la @CHECK_LIBS@
gu_testspp_LDFLAGS = @AM_LDFLAGS@ -static

gu_to_test_SOURCES = gu_to_test.c
gu_to_test_LDADD   = ../src/libgalerautils.la

TESTS = $(noinst_PROGRAMS)

# all: check

#
