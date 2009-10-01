lib_LTLIBRARIES = libgcommtransportpp.la

INCLUDES = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/transport/include

libgcommtransportpp_la_SOURCES = \
	transport.cpp \
	transport_tcp.cpp \
	transport_tipc.cpp
libgcommtransportpp_la_CXXFLAGS = @AM_CXXFLAGS@ -Wno-conversion