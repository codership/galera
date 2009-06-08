# Copyright (C) 2009 Codership Oy <info@codership.com>
#

# Initialize AM_CFLAGS to empty.
AM_CFLAGS = 
#if ENABLE_DEBUG
#endif

GCACHE_SRCS =               \
	Exception.cpp       \
	FileDescriptor.cpp  \
	MMap.cpp            \
	GCache.cpp          \
	GCache_header.cpp   \
	GCache_memops.cpp   \
	GCache_seqno.cpp

#GCACHE_INCS = $(wildcard *.hpp)

# Describe libGCACHE library target
#include_HEADERS   = GCache.hpp
lib_LTLIBRARIES      = libgcache.la
libgcache_la_SOURCES = $(GCACHE_SRCS) $(GCACHE_INCS)
INTERFACE = 1
REVISION  = 0
AGE       = 0
libgcache_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

# Desclibe GCACHE_test binary target
noinst_PROGRAMS  = test
test_SOURCES     = test.cpp
test_LDADD       = $(lib_LTLIBRARIES)
test_LDFLAGS     = -static

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

#test: GCACHE_test

#%.o: %.c $(include_HEADERS) Makefile
#	@CC@ @CFLAGS@ $(AM_CFLAGS) -c -o $@ $<

tags: TAGS

#SUBDIRS = unit_tests
#
