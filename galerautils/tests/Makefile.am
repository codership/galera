noinst_PROGRAMS   = gu_tests
gu_tests_SOURCES  =                      \
	gu_tests.c                       \
	gu_mem_test.c   gu_mem_test.h    \
	gu_bswap_test.c gu_bswap_test.h  \
	gu_dbug_test.c  gu_dbug_test.h

gu_tests_INCLUDES = @CHECK_CFLAGS@
gu_tests_LDADD    = ../src/libgalerautils.la @CHECK_LIBS@

TESTS             = $(noinst_PROGRAMS)

all: check

#
