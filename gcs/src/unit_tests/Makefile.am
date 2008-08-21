# Copyright (C) 2007 Codership Oy <info@codership.com>
#
# $Id$

AM_CFLAGS = -I..

# gcs_to_test is separate for benchmarking purposes
noinst_PROGRAMS   = gcs_tests gcs_to_test

gcs_tests_SOURCES =           \
	gcs_tests.c           \
	../gcs_error.c        \
	gcs_fifo_test.c       \
	../gcs_fifo_lite.c    \
	gcs_comp_test.c       \
	../gcs_comp_msg.c     \
	gcs_backend_test.c    \
	../gcs_backend.c      \
	gcs_proto_test.c      \
	../gcs_act_proto.c    \
	gcs_defrag_test.c     \
	../gcs_defrag.c       \
	gcs_node_test.c       \
	../gcs_node.c         \
	gcs_group_test.c      \
	../gcs_group.c
#	gcs_uuid_test.c       \
#	../gcs_uuid.c

# For gcs_backend_test
gcs_tests_CPPFLAGS = -DGCS_USE_SPREAD -DGCS_USE_VS
gcs_tests_INCLUDES = @CHECK_CFLAGS@
gcs_tests_LDADD    = @CHECK_LIBS@

gcs_to_test_SOURCES =       \
	gcs_to_test.c       \
	../gcs_to.c

gcs_to_test_INCLUDES = @CHECK_CFLAGS@
gcs_to_test_LDADD    = @CHECK_LIBS@

TESTS              = $(noinst_PROGRAMS)

all: check
