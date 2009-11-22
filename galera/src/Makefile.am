# Copyright (C) 2007-2009 Codership Oy <info@codership.com>

# <libgalera>
include_HEADERS =
lib_LTLIBRARIES = libmmgalera.la
libmmgalera_la_SOURCES =   \
	galera_info.c      \
	galera_state.c     \
	galera_options.c   \
	galera_status.c    \
	galera_job_queue.c \
	mm_galera.c

include_HEADERS =          \
	galera_info.h      \
	galera_state.h     \
	galera_options.h   \
	galera_status.h    \
	galera_job_queue.h

#GALERA_RELEASE =
#if ENABLE_RELEASE
#GALERA_RELEASE = -Wl,--whole-archive .libs/libwsdb.a -Wl,--no-whole-archive
#endif
#libmmgalera_la_DEPENDENCIES = libwsdb.la

# The following line is required for correct symbol resolution during dlopen()
libmmgalera_la_LIBADD = -lwsdb

GALERA_INTERFACE = @WSREP_INTERFACE_VERSION@
GALERA_REVISION  = 0
GALERA_AGE       = 0
libmmgalera_la_LDFLAGS = @AM_LDFLAGS@ \
	-version-info $(GALERA_INTERFACE):$(GALERA_REVISION):$(GALERA_AGE)
# </libgalera>
