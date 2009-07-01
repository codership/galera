# Copyright (C) 2008 Codership Oy <info@codership.com>
#
# $Id$

# Initialize AM_CFLAGS to empty.
AM_CFLAGS = -fno-strict-aliasing
if ENABLE_DEBUG
AM_CFLAGS += -DGCS_DEBUG_GCS
AM_CFLAGS += -DGCS_DEBUG_QUEUE
AM_CFLAGS += -DGCS_DEBUG_PROTO
AM_CFLAGS += -DGCS_DEBUG_SPREAD
AM_CFLAGS += -DGCS_DEBUG_CORE
AM_CFLAGS += -DGCS_LOGGING
endif

GCS_SRCS = \
	gcs_conf.c      \
	gcs_fifo_lite.c \
	gcs_queue.c     \
	gcs_msg_type.c  \
	gcs_comp_msg.c  \
	gcs_backend.c   \
	gcs_dummy.c     \
	gcs_act_proto.c \
	gcs_defrag.c    \
	gcs_state.c     \
	gcs_node.c      \
	gcs_group.c     \
	gcs_core.c      \
	gcs.c

if WITH_SPREAD
    AM_CFLAGS += -DGCS_USE_SPREAD
    GCS_SRCS  += gcs_spread.c
endif


if ENABLE_GCOMM
    AM_CFLAGS += -DGCS_USE_GCOMM
    GCS_SRCS  += gcs_gcomm.cpp
endif

if ENABLE_VS
    AM_CFLAGS += -DGCS_USE_VS
    GCS_SRCS  += gcs_vs.cpp
endif


GCS_INCS = $(wildcard *.h)

# Describe libgcs library target
include_HEADERS   = gcs.h
lib_LTLIBRARIES   = libgcs.la
libgcs_la_SOURCES = $(GCS_SRCS) $(GCS_INCS)
INTERFACE = 10
REVISION  = 0
AGE       = 0
libgcs_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)
libgcs_la_DEPENDENCIES = $(GCOMM_LIBS)


# Desclibe gcs_test binary target
noinst_PROGRAMS  = gcs_test
gcs_test_SOURCES = gcs_test.c
gcs_test_LDADD   = $(lib_LTLIBRARIES) $(GCOMM_LIBS)
gcs_test_LDFLAGS = -static
gcs_test_LINK    = $(LIBTOOL) --tag=CXX --mode=link $(CCLD) -o $@

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

test: gcs_test

#%.o: %.c $(include_HEADERS) Makefile
#	@CC@ @CFLAGS@ $(AM_CFLAGS) -c -o $@ $<

tags: TAGS

#SUBDIRS = unit_tests
#
