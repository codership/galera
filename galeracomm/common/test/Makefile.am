noinst_PROGRAMS = check_commonpp
# check_common
INCLUDES = -I$(top_srcdir)/common/include

check_commonpp_SOURCES = check_commonpp.cpp
check_commonpp_LDFLAGS = -lcheck -lpthread
check_commonpp_LDADD = $(top_builddir)/common/src/libgcommcommonpp.la
