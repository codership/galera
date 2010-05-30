// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_sm_test.h"
#include "../gcs_sm.h"

START_TEST (gcs_sm_test_basic)
{
    gcs_sm_t* sm = gcs_sm_create(4);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    int ret = gcs_sm_enter(sm, &cond);
    gu_cond_destroy(&cond);
    fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));

    gcs_sm_leave(sm);

    ret = gcs_sm_close(sm);
    fail_if(ret);

    gcs_sm_destroy(sm);
}
END_TEST

static int order = 0; // variable to trac the order of events

static void* closing_thread (void* data)
{
    gcs_sm_t* sm = (gcs_sm_t*)data;

    fail_if(order != 1);
    int ret = gcs_sm_close(sm);
    fail_if(ret);
    fail_if(order != 2);

    gcs_sm_destroy(sm);
    return NULL;
}

START_TEST (gcs_sm_test_close)
{
    gcs_sm_t* sm = gcs_sm_create(4);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    int ret = gcs_sm_enter(sm, &cond);
    gu_cond_destroy(&cond);
    fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    fail_if(order != 0);

    order = 1;
    gu_thread_t tmp;
    gu_thread_create (&tmp, NULL, closing_thread, sm);
    sleep(1);
    order = 2;
    gcs_sm_leave(sm);
}
END_TEST

Suite *gcs_send_monitor_suite(void)
{
  Suite *s  = suite_create("GCS send monitor");
  TCase *tc = tcase_create("gcs_sm");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gcs_sm_test_basic);
  tcase_add_test  (tc, gcs_sm_test_close);
  return s;
}

