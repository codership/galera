# Copyright (C) 2007-2009 Codership Oy <info@codership.com>

# <libgalera>
include_HEADERS =
lib_LTLIBRARIES = libmmgalera.la
libmmgalera_la_SOURCES =   \
	mm_galera.c        \
	galera_info.c      \
	galera_state.c     \
	galera_job_queue.c


#GALERA_RELEASE =
#if ENABLE_RELEASE
#GALERA_RELEASE = -Wl,--whole-archive .libs/libwsdb.a -Wl,--no-whole-archive
#endif
#libmmgalera_la_DEPENDENCIES = libwsdb.la
#libmmgalera_la_LIBADD = -lwsdb
GALERA_INTERFACE = @WSREP_INTERFACE_VERSION@
GALERA_REVISION  = 0
GALERA_AGE       = 0
libmmgalera_la_LDFLAGS = @AM_LDFLAGS@ \
	-version-info $(GALERA_INTERFACE):$(GALERA_REVISION):$(GALERA_AGE)
# </libgalera>
