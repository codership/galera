# Copyright (C) 2007 Codership Oy <info@codership.com>
#
# $Id$

AM_CFLAGS = -I..

noinst_PROGRAMS   = gcs_tests

gcs_tests_SOURCES =           \
	gcs_tests.c           \
	gcs_fifo_test.c       \
	../gcs_fifo_lite.c    \
	gcs_comp_test.c       \
	../gcs_comp_msg.c     \
	gcs_state_test.c      \
	../gcs_state.c        \
	gcs_backend_test.c    \
	../gcs_backend.c      \
	gcs_proto_test.c      \
	../gcs_act_proto.c    \
	gcs_defrag_test.c     \
	../gcs_defrag.c       \
	gcs_node_test.c       \
	../gcs_node.c         \
	gcs_group_test.c      \
	../gcs_group.c        \
	gcs_core_test.c       \
	../gcs_core.c         \
	../gcs_dummy.c        \
	../gcs_msg_type.c

# For gcs_backend_test
gcs_tests_CPPFLAGS = -DGCS_USE_SPREAD -DGCS_USE_VS

# For gcs_core_test
gcs_tests_CPPFLAGS += -DGCS_CORE_TESTING -DGCS_DUMMY_TESTING

gcs_tests_INCLUDES = @CHECK_CFLAGS@
gcs_tests_LDADD    = @CHECK_LIBS@

TESTS              = $(noinst_PROGRAMS)

all: check
