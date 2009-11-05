# Copyright (C) 2007 Codership Oy <info@codership.com>
#
# $Id$

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
	gu_lock_step.h \
	gu_options.h   \
	galerautils.hpp  \
	gu_exception.hpp \
	gu_throw.hpp     \
	gu_logger.hpp    \
	gu_assert.hpp    \
	gu_utils.hpp     \
	gu_regex.hpp     \
	gu_mutex.hpp     \
	gu_cond.hpp      \
	gu_lock.hpp      \
	gu_uri.hpp       \
	gu_network.hpp   \
	gu_datetime.hpp

lib_LTLIBRARIES = libgalerautils.la

libgalerautils_la_SOURCES = \
	gu_log.c       \
	gu_mem.c       \
	gu_mutex.c     \
	gu_dbug.c      \
	gu_fifo.c      \
	gu_uuid.c      \
	gu_to.c        \
	gu_lock_step.c \
	gu_options.c

INTERFACE = 11
REVISION  = 0
AGE       = 1
libgalerautils_la_LDFLAGS = @AM_LDFLAGS@ \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

lib_LTLIBRARIES += libgalerautils++.la

libgalerautils___la_SOURCES = \
	gu_exception.cpp      \
	gu_string.cpp         \
	gu_buffer.cpp         \
	gu_logger.cpp         \
	gu_regex.cpp          \
	gu_uri.cpp            \
	gu_resolver.cpp	      \
	gu_epoll.cpp	      \
	gu_network.cpp        \
	gu_prodcons.cpp	      \
	gu_datetime.cpp


INTERFACEPP = 4
REVISIONPP  = 0
AGEPP       = 0
libgalerautils___la_LDFLAGS = @AM_LDFLAGS@ \
	-version-info $(INTERFACEPP):$(REVISIONPP):$(AGEPP)

tags: TAGS

#
