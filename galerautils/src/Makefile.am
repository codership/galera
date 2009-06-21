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
	gu_macros.h    \
	gu_log.h       \
	gu_assert.h    \
	gu_mem.h       \
	gu_mutex.h     \
	gu_conf.h      \
	gu_dbug.h      \
	gu_time.h      \
	gu_fifo.h      \
	gu_byteswap.h  \
	gu_uuid.h      \
	gu_to.h        \
	galerautils.hpp  \
	gu_exception.hpp \
	gu_logger.hpp    \
	gu_assert.hpp    \
	gu_mutex.hpp     \
	gu_cond.hpp      \
	gu_lock.hpp

lib_LTLIBRARIES = libgalerautils.la

libgalerautils_la_SOURCES = \
	gu_log.c   \
	gu_mem.c   \
	gu_mutex.c \
	gu_dbug.c  \
	gu_fifo.c  \
	gu_uuid.c  \
	gu_to.c

INTERFACE = 9
REVISION  = 0
AGE       = 2
libgalerautils_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

lib_LTLIBRARIES += libgalerautils++.la

libgalerautils___la_SOURCES = \
	gu_exception.cpp \
	gu_logger.cpp


INTERFACEPP = 1
REVISIONPP  = 0
AGEPP       = 0
libgalerautils___la_LDFLAGS = \
	-version-info $(INTERFACEPP):$(REVISIONPP):$(AGEPP)

tags: TAGS

#
