# Copyright (C) 2007 Codership Oy <info@codership.com>
#
# $Id$

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
	gu_time.h      \
	gu_fifo.h      \
	gu_byteswap.h  \
	gu_uuid.h

lib_LTLIBRARIES = libgalerautils.la

libgalerautils_la_SOURCES = \
	gu_log.c   \
	gu_mem.c   \
	gu_mutex.c \
	gu_dbug.c  \
	gu_fifo.c  \
	gu_uuid.c

INTERFACE = 6
REVISION  = 0
AGE       = 5
libgalerautils_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

tags: TAGS

#
