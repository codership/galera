
lib_LTLIBRARIES = libgcommvspp.la libgcommvsbes.la libgcommevs.la
sbin_PROGRAMS = vsbes

INCLUDES = \
	-I$(top_srcdir)/common/include \
	-I$(top_srcdir)/vs/include \
	-I$(top_srcdir)/transport/include

libgcommvspp_la_SOURCES = vs.cpp vs_backend.cpp vs_fifo_backend.cpp vs_remote_backend.cpp
libgcommvspp_la_CXXFLAGS =
libgcommvspp_la_LDFLAGS = 
libgcommvspp_la_LIBADD = \
	$(top_builddir)/transport/src/libgcommtransportpp.la \
	$(top_builddir)/common/src/libgcommcommonpp.la

libgcommvsbes_la_SOURCES = vsbes.cpp
libgcommvsbes_la_CXXFLAGS = 
libgcommvsbes_la_LIBADD = libgcommvspp.la

vsbes_SOURCES = vsbes.cpp
vsbes_CXXFLAGS = -DCOMPILE_SERVER
vsbes_LDADD = libgcommvspp.la \
	$(top_builddir)/transport/src/libgcommtransportpp.la \
	$(top_builddir)/common/src/libgcommcommonpp.la

libgcommevs_la_SOURCES = evs.cpp evs.hpp \
	evs_proto.cpp evs_proto.hpp \
	evs_input_map.cpp evs_input_map.hpp evs_message.hpp
