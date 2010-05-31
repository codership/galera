// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_sm_test.h"
#include "../gcs_sm.h"

#define TEST_USLEEP 100000 // 100 ms

START_TEST (gcs_sm_test_basic)
{
    int ret;

    gcs_sm_t* sm = gcs_sm_create(2, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    int i;
    for (i = 1; i < 5; i++) {
        ret = gcs_sm_enter(sm, &cond, false);
        fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
        fail_if(sm->wait_q_len != 0, "wait_q_len = %ld, expected 0",
                sm->wait_q_len);
        fail_if(sm->entered != true, "entered = %d, expected %d",
                sm->wait_q_len, true);

        gcs_sm_leave(sm);
        fail_if(sm->entered != false, "entered = %d, expected %d",
                sm->wait_q_len, false);
    }

    ret = gcs_sm_close(sm);
    fail_if(ret);

    gcs_sm_destroy(sm);
    gu_cond_destroy(&cond);
}
END_TEST

static volatile int order = 0; // global variable to trac the order of events

static void* closing_thread (void* data)
{
    gcs_sm_t* sm = (gcs_sm_t*)data;

    fail_if(order != 0, "order is %d, expected 0", order);
    int ret = gcs_sm_close(sm);
    fail_if(ret);
    fail_if(order != 1, "order is %d, expected 1", order);

    gcs_sm_destroy(sm);
    return NULL;
}

START_TEST (gcs_sm_test_close)
{
    gcs_sm_t* sm = gcs_sm_create(2, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    int ret = gcs_sm_enter(sm, &cond, false);
    fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    fail_if(sm->wait_q_len != 0, "wait_q_len = %ld, expected 0",
            sm->wait_q_len);
    fail_if(order != 0);

    gu_thread_t thr;
    gu_thread_create (&thr, NULL, closing_thread, sm);
    usleep(TEST_USLEEP);
    order = 1;
    fail_if(sm->wait_q_len != 1, "wait_q_len = %ld, expected 1",
            sm->wait_q_len);

    gu_info ("Started close thread, wait_q_len = %ld", sm->wait_q_len);

    gcs_sm_leave(sm);

    gu_cond_destroy(&cond);
}
END_TEST

static void* pausing_thread (void* data)
{
    gcs_sm_t* sm = (gcs_sm_t*)data;

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    gcs_sm_schedule (sm);
    fail_if (order != 0, "order = %d, expected 0");
    order = 1;
    gcs_sm_enter (sm, &cond, true);
    fail_if (order != 2, "order = %d, expected 2");
    order = 3;
    gcs_sm_leave (sm);

    gu_cond_destroy(&cond);

    return NULL;
}

START_TEST (gcs_sm_test_pause)
{
    gcs_sm_t* sm = gcs_sm_create(4, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    gu_thread_t thr;

    // Test attempt to enter paused monitor
    order = 0;
    gcs_sm_pause (sm);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    usleep (TEST_USLEEP);
    fail_if (order != 1, "order = %d, expected 1");
    order = 2;
    gcs_sm_continue (sm);
    gu_thread_join (thr, NULL);
    fail_if (order != 3, "order = %d, expected 3");

    // Testing scheduling capability
    gcs_sm_schedule (sm);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    usleep (TEST_USLEEP);
    fail_if (order != 3, "order = %d, expected 3");
    order = 0;

    int ret = gcs_sm_enter(sm, &cond, true);
    fail_if (ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    fail_if (sm->wait_q_len != 0, "wait_q_len = %ld, expected 0",
            sm->wait_q_len);
    usleep  (TEST_USLEEP);
    fail_if (order != 1, "order = %d, expected 1");
    fail_if (sm->wait_q_len != 1, "wait_q_len = %ld, expected 1",
             sm->wait_q_len);

    gu_info ("Started pause thread, wait_q_len = %ld", sm->wait_q_len);

    // Now test pausing when monitor is in entered state
    order = 2;
    gcs_sm_pause (sm);
    usleep (TEST_USLEEP);
    gcs_sm_continue (sm); // nothing should continue, since monitor is entered
    usleep (TEST_USLEEP);
    fail_if (order != 2, "order = %d, expected 2");

    // Now test pausing when monitor is left
    gcs_sm_pause (sm);
    fail_if (sm->wait_q_len != 1, "wait_q_len = %ld, expected 1",
             sm->wait_q_len);
    gcs_sm_leave (sm);
    usleep (TEST_USLEEP);
    fail_if (order != 2, "order = %d, expected 2");

    gcs_sm_continue (sm); // paused thread should continue
    gcs_sm_enter (sm, &cond, false);
    fail_if (order != 3, "order = %d, expected 3");
    gcs_sm_leave (sm);

    gu_cond_destroy(&cond);
    gcs_sm_close (sm);
    gcs_sm_destroy (sm);
}
END_TEST

Suite *gcs_send_monitor_suite(void)
{
  Suite *s  = suite_create("GCS send monitor");
  TCase *tc = tcase_create("gcs_sm");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gcs_sm_test_basic);
  tcase_add_test  (tc, gcs_sm_test_close);
  tcase_add_test  (tc, gcs_sm_test_pause);
  return s;
}

