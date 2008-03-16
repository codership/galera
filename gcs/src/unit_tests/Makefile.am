# Copyright (C) 2007 Codership Oy <info@codership.com>
AM_CFLAGS = -I..
noinst_PROGRAMS   = gcs_tests
gcs_tests_SOURCES =                        \
	gcs_tests.c                        \
	gcs_comp_test.c                    \
	../gcs_comp_msg.c                  \
	gcs_proto_test.c                   \
	../gcs_act_proto.c                  \
	gcs_uuid_test.c                    \
	../gcs_uuid.c

gcs_tests_INCLUDES = @CHECK_CFLAGS@
gcs_tests_LDADD    = @CHECK_LIBS@

TESTS              = $(noinst_PROGRAMS)

all: check
