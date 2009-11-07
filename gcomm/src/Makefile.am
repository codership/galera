
lib_LTLIBRARIES = libgcomm.la 

# Built separately due to -Wno-conversion
noinst_LTLIBRARIES = libgcommutil.la
libgcommutil_la_SOURCES = util.cpp
libgcommutil_la_CXXFLAGS = $(AM_CXXFLAGS) -Wno-conversion


libgcomm_la_SOURCES = 		\
	defaults.cpp            \
	histogram.cpp		\
	uuid.cpp		\
	view.cpp		\
	protostack.cpp		\
	transport.cpp		\
	gmcast_proto.cpp        \
	gmcast.cpp		\
	evs_message2.cpp	\
	evs_input_map2.cpp 	\
	evs_node.cpp		\
	evs_consensus.cpp	\
	evs_proto.cpp		\
	pc_proto.cpp		\
	pc.cpp

libgcomm_la_LIBADD = libgcommutil.la
