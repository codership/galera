
noinst_PROGRAMS = check_vspp

INCLUDES = \
	-I$(top_srcdir)/vs/src \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/transport/include \
	-I$(top_srcdir)/vs/include

check_vspp_SOURCES = check_vspp.cpp
check_vspp_CXXFLAGS =
check_vspp_LDFLAGS = -lcheck
check_vspp_LDADD = $(top_builddir)/vs/src/libgcommvspp.la \
	$(top_builddir)/common/src/libgcommcommonpp.la
