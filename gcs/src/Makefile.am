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
	gcs_error.c     \
	gcs_fifo.c      \
	gcs_queue.c     \
	gcs_to.c        \
	gcs_uuid.c      \
	gcs_comp_msg.c  \
	gcs_dummy.c     \
	gcs_backend.c   \
	gcs_act_proto.c \
	gcs_core.c      \
	gcs.c

if ENABLE_SPREAD
    AM_CFLAGS += -DGCS_USE_SPREAD
    GCS_SRCS  += gcs_spread.c
endif

if ENABLE_VS
    AM_CFLAGS += -DGCS_USE_VS
    GCS_SRCS  += gcs_vs.cpp
# Enforce C++ linker
    CCLD=$(CXX)
    GCOMM_LIBS = -lgcommcommonpp -lgcommtransportpp -lgcommvspp
endif

GCS_INCS = $(wildcard *.h)

# Describe libgcs library target
include_HEADERS   = gcs.h
lib_LTLIBRARIES   = libgcs.la
libgcs_la_SOURCES = $(GCS_SRCS) $(GCS_INCS)
libgcs_la_LIBADD = $(GCOMM_LIBS)
#libgcs_la_LDADD =
INTERFACE = 3
REVISION  = 0
AGE       = 0
libgcs_la_LDFLAGS = \
	-version-info $(INTERFACE):$(REVISION):$(AGE)

# Desclibe gcs_test binary target
noinst_PROGRAMS  = gcs_test
gcs_test_SOURCES = gcs_test.c
gcs_test_LDADD   = $(lib_LTLIBRARIES) $(GCOMM_LIBS)
gcs_test_LDFLAGS = -static

.PHONY: lib test tags

lib:  $(lib_LTLIBRARIES)

test: gcs_test

#%.o: %.c $(include_HEADERS) Makefile
#	@CC@ @CFLAGS@ $(AM_CFLAGS) -c -o $@ $<

tags: TAGS

#SUBDIRS = unit_tests
#
