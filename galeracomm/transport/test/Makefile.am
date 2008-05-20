TESTS = check_transportpp
noinst_PROGRAMS = check_transportpp 
bin_PROGRAMS = perftest

INCLUDES = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/transport/include

check_transportpp_SOURCES = check_transportpp.cpp
check_transportpp_LDADD = \
	$(top_builddir)/common/src/libgcommcommonpp.la \
	$(top_builddir)/transport/src/libgcommtransportpp.la
check_transportpp_LDFLAGS = -lcheck

perftest_SOURCES = perftest.cpp
perftest_LDADD = \
	$(top_builddir)/common/src/libgcommcommonpp.la \
	$(top_builddir)/transport/src/libgcommtransportpp.la