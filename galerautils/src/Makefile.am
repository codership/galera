# Copyright (C) 2007 Codership Oy <info@codership.com>
# Initialize AM_CFLAGS to empty. 
#AM_CFLAGS =
#if ENABLE_DEBUG
#AM_CFLAGS += -DGLR_DEBUG_GLR
#AM_CFLAGS += -DGLR_DEBUG_QUEUE
#AM_CFLAGS += -DGLR_LOGGING
#endif

# Describe libgalerautils library target
include_HEADERS =      \
	galerautils.h  \
	gu_log.h       \
	gu_assert.h    \
	gu_mem.h       \
	gu_mutex.h     \
	gu_conf.h      \
	gu_dbug.h      \
	gu_byteswap.h

lib_LTLIBRARIES = libgalerautils.la

libgalerautils_la_SOURCES = \
	gu_log.c   \
	gu_mem.c   \
	gu_mutex.c \
	gu_dbug.c

INTERFACE = 3
REVISION  = 3
AGE       = 2
libgalerautils_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

tags: TAGS

#
