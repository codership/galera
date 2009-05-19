
TESTS = check_gcomm
noinst_PROGRAMS = check_gcomm

INCLUDES = -I../src

check_gcomm_SOURCES = 		\
	check_types.cpp 	\
	check_util.cpp		\
	check_buffer.cpp 	\
	check_event.cpp		\
	check_concurrent.cpp	\
	check_tcp.cpp		\
	check_gmcast.cpp	\
	check_evs.cpp		\
	check_vs.cpp		\
	check_gcomm.cpp
check_gcomm_LDADD = $(top_builddir)/src/libgcomm.la
check_gcomm_LDFLAGS = -lcheck -static