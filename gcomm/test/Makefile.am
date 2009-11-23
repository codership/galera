TESTS = check_gcomm
noinst_PROGRAMS = check_gcomm

INCLUDES = -I../src

include_HEADERS =       \
	check_gcomm.hpp \
	check_templ.hpp \
	check_trace.hpp

check_gcomm_SOURCES =    \
	check_types.cpp  \
	check_util.cpp   \
	check_gmcast.cpp \
	check_trace.cpp  \
	check_evs2.cpp   \
	check_pc.cpp     \
	check_gcomm.cpp

check_gcomm_LDADD = \
	$(top_builddir)/src/libgcomm.la \
	-lgalerautils                   \
	-lgalerautils++                 \
	@CHECK_LIBS@

check_gcomm_LDFLAGS = @AM_LDFLAGS@ -static
check_gcomm_DEPENDNCIES = -lgalerautils++

