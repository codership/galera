# Copyright (C) 2009 Codership Oy <info@codership.com>
#

GCACHE_SRCS =               \
	FileDescriptor.cpp  \
	MMap.cpp            \
	GCache.cpp          \
	GCache_header.cpp   \
	GCache_memops.cpp   \
	GCache_seqno.cpp

# Describe libGCACHE library target
#include_HEADERS   = GCache.hpp
lib_LTLIBRARIES      = libgcache.la
libgcache_la_SOURCES = $(GCACHE_SRCS) $(GCACHE_INCS)
INTERFACE = 1
REVISION  = 0
AGE       = 0
libgcache_la_LDFLAGS = \
	@AM_LDFLAGS@ -version-info $(INTERFACE):$(REVISION):$(AGE)

# Desclibe GCACHE_test binary target
noinst_PROGRAMS  = test
test_SOURCES     = test.cpp
test_LDADD       = $(lib_LTLIBRARIES)
test_LDFLAGS     = @AM_LDFLAGS@ -static

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

#test: GCACHE_test

tags: TAGS

#SUBDIRS = unit_tests
#
