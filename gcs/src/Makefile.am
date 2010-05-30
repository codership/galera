# Copyright (C) 2008 Codership Oy <info@codership.com>
#
# $Id$

AM_CFLAGS += -fno-strict-aliasing

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
	gcs_sm.c        \
	gcs_fifo_lite.c \
	gcs_msg_type.c  \
	gcs_comp_msg.c  \
	gcs_backend.c   \
	gcs_dummy.c     \
	gcs_act_proto.c \
	gcs_defrag.c    \
	gcs_state_msg.c \
	gcs_node.c      \
	gcs_group.c     \
	gcs_core.c      \
	gcs.c           \
\
	gcs_act.h       \
	gcs_act_proto.h \
	gcs_backend.h   \
	gcs_comp_msg.h  \
	gcs_core.h      \
	gcs_defrag.h    \
	gcs_defs.h      \
	gcs_dummy.h     \
	gcs_fifo_lite.h \
	gcs_group.h     \
	gcs_msg_type.h  \
	gcs_node.h      \
	gcs_recv_msg.h  \
	gcs_seqno.h     \
	gcs_sm.h        \
	gcs_state_msg.h

if WITH_SPREAD
    AM_CFLAGS += -DGCS_USE_SPREAD
    GCS_SRCS  += gcs_spread.c gcs_spread.h
endif

if ENABLE_GCOMM
    AM_CFLAGS += -DGCS_USE_GCOMM
    GCS_SRCS  += gcs_gcomm.cpp gcs_gcomm.h
endif

if ENABLE_VS
    AM_CFLAGS += -DGCS_USE_VS
    GCS_SRCS  += gcs_vs.cpp gcs_vs.h
endif

# Describe libgcs library target
include_HEADERS   = gcs.h
lib_LTLIBRARIES   = libgcs.la
libgcs_la_SOURCES = $(GCS_SRCS)
INTERFACE = 16
REVISION  = 1
AGE       = 1
libgcs_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)
libgcs_la_DEPENDENCIES = $(GCOMM_LIBS)


# Desclibe gcs_test binary target
noinst_PROGRAMS  = gcs_test
gcs_test_SOURCES = gcs_test.c gcs_test.h
gcs_test_LDADD   = $(lib_LTLIBRARIES) $(GCOMM_LIBS) 
gcs_test_LDFLAGS = -static
# how to set it up automatically? $(CXXFLAGS) - to use the right arch
CCLD=g++ $(CXXFLAGS)
gcs_test_LINK    = $(LIBTOOL) --tag=CXX --mode=link $(CCLD) -static -o $@

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

test: gcs_test

#%.o: %.c $(include_HEADERS) Makefile
#	@CC@ @CFLAGS@ $(AM_CFLAGS) -c -o $@ $<

tags: TAGS

#SUBDIRS = unit_tests
#
