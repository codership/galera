// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include <string.h>
#include "gcs_sm_test.h"
#include "../gcs_sm.h"

#define TEST_USLEEP 10000 // 10 ms

/* we can't use pthread functions for waiting for certain conditions */
#define WAIT_FOR(cond)                          \
    { int count = 100; while (--count && !(cond)) { usleep (1000); }}

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

#if 0 // looks like we have to use sleeps here lest we run into deadlock
static pthread_mutex_t _mtx  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  _cond = PTHREAD_COND_INITIALIZER;

static void crit_enter()
{
    pthread_mutex_lock (&_mtx);
}

static void crit_wait()
{
    pthread_cond_signal (&_cond);
    pthread_cond_wait (&_cond, &_mtx);
}

static void crit_leave()
{
    pthread_cond_signal (&_cond);
    pthread_mutex_unlock (&_mtx);
}
#endif

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
    order = 0;

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

static volatile int pause_order = 0;

static void* pausing_thread (void* data)
{
    gu_info ("pausing_thread start, pause_order = %d", pause_order);
    gcs_sm_t* sm = (gcs_sm_t*)data;

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    gcs_sm_schedule (sm);
    gu_info ("pausing_thread scheduled, pause_order = %d", pause_order);
    fail_if (pause_order != 0, "pause_order = %d, expected 0");
    pause_order = 1;
    gcs_sm_enter (sm, &cond, true);
    gu_info ("pausing_thread entered, pause_order = %d", pause_order);
    fail_if (pause_order != 2, "pause_order = %d, expected 2");
    pause_order = 3;
    gcs_sm_leave (sm);

    gu_cond_destroy(&cond);

    gu_info ("pausing_thread exit, pause_order = %d", pause_order);
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
    pause_order = 0;
    gcs_sm_pause (sm);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    WAIT_FOR(1 == pause_order);
    fail_if (pause_order != 1, "pause_order = %d, expected 1");
    pause_order = 2;
    gcs_sm_continue (sm);
    gu_thread_join (thr, NULL);
    fail_if (pause_order != 3, "pause_order = %d, expected 3");

    // Testing scheduling capability
    gcs_sm_schedule (sm);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    usleep (TEST_USLEEP);
    // no changes in pause_order
    fail_if (pause_order != 3, "pause_order = %d, expected 3");
    pause_order = 0;

    int ret = gcs_sm_enter(sm, &cond, true);
    fail_if (ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    // released monitor lock, thr should continue and schedule,
    // set pause_order to 1
    WAIT_FOR(1 == pause_order);
    fail_if (pause_order != 1, "pause_order = %d, expected 1");
    fail_if (sm->wait_q_len != 1, "wait_q_len = %ld, expected 1",
             sm->wait_q_len);

    gu_info ("Started pause thread, wait_q_len = %ld", sm->wait_q_len);

    // Now test pausing when monitor is in entered state
    pause_order = 2;
    gcs_sm_pause (sm);
    usleep (TEST_USLEEP);
    gcs_sm_continue (sm); // nothing should continue, since monitor is entered
    usleep (TEST_USLEEP);
    fail_if (pause_order != 2, "pause_order = %d, expected 2");
    fail_if (sm->entered != 1, "entered = %ld, expected 1", sm->entered);

    // Now test pausing when monitor is left
    gcs_sm_pause (sm);
    fail_if (sm->wait_q_len != 1, "wait_q_len = %ld, expected 1",
             sm->wait_q_len);
    gcs_sm_leave (sm);
    fail_if (sm->wait_q_len != 0, "wait_q_len = %ld, expected 0",
             sm->wait_q_len);
    fail_if (sm->entered != 0, "entered = %ld, expected 1", sm->entered);
    usleep (TEST_USLEEP); // nothing should change, since monitor is paused
    fail_if (pause_order != 2, "pause_order = %d, expected 2");

    fail_if (sm->entered != 0, "wait_q_len = %ld, expected 0",
             sm->entered);
    gcs_sm_continue (sm); // paused thread should continue
    gcs_sm_enter (sm, &cond, false);
    fail_if (sm->entered != 1, "wait_q_len = %ld, expected 1",
             sm->entered);
    fail_if (sm->wait_q_len != 0, "wait_q_len = %ld, expected 0",
             sm->wait_q_len);
//    fail_if (pause_order != 3, "pause_order = %d, expected 3");
    gcs_sm_leave (sm);

    gu_cond_destroy(&cond);
    gcs_sm_close (sm);
    gcs_sm_destroy (sm);
}
END_TEST

static volatile long global_handle = 0;
static volatile long global_ret = 0;

static void* interrupt_thread(void* arg)
{
    gcs_sm_t* sm = (gcs_sm_t*) arg;

    global_handle = gcs_sm_schedule (sm);

    if (global_handle >= 0) {
        pthread_cond_t cond;
        pthread_cond_init (&cond, NULL);

        if (0 == (global_ret = gcs_sm_enter (sm, &cond, true))) {
            gcs_sm_leave (sm);
        }
        pthread_cond_destroy (&cond);
    }

    return NULL;
}

#define TEST_CREATE_THREAD(t, h, q)                                     \
    global_handle = -1;                                                 \
    pthread_create ((t), NULL, interrupt_thread, sm);                   \
    WAIT_FOR(global_handle == (h));                                     \
    fail_if (global_handle != (h), "global_handle = %ld, expected %ld", \
             global_handle, (h));                                       \
    fail_if (sm->wait_q_len != (q), "wait_q_len = %ld, expected %ld",   \
                 sm->wait_q_len, (q));

#define TEST_INTERRUPT_THREAD(h, t)                                     \
    ret = gcs_sm_interrupt (sm, (h));                                   \
    fail_if (ret != 0);                                                 \
    pthread_join ((t), NULL);                                           \
    fail_if (global_ret != -EINTR, "global_ret = %ld, expected %ld (-EINTR)", \
             global_ret, -EINTR);


START_TEST (gcs_sm_test_interrupt)
{
    gcs_sm_t* sm = gcs_sm_create(4, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    gu_thread_t thr1;
    gu_thread_t thr2;
    gu_thread_t thr3;

    long handle = gcs_sm_schedule (sm);
    fail_if (handle != 0, "handle = %ld, expected 0");

    long ret = gcs_sm_enter (sm, &cond, true);
    fail_if (ret != 0);

    /* 1. Test interrupting blocked by previous thread */
    TEST_CREATE_THREAD(&thr1, 2, 1);

    TEST_CREATE_THREAD(&thr2, 3, 2);

    TEST_INTERRUPT_THREAD(2, thr1);

    gcs_sm_leave (sm); // this should let 2nd enter monitor
    pthread_join (thr2, NULL);
    fail_if (global_ret != 0, "global_ret = %ld, expected 0", global_ret);
    fail_if (sm->wait_q_len != -1, "wait_q_len = %ld, expected %ld",
             sm->wait_q_len, -1);

    ret = gcs_sm_interrupt (sm, 3); // try to interrupt 2nd which has exited
    fail_if (ret != -ESRCH);

    /* 2. Test interrupting blocked by pause */
    gcs_sm_pause (sm);

    TEST_CREATE_THREAD(&thr1, 3, 0);

    TEST_INTERRUPT_THREAD(3, thr1);

    TEST_CREATE_THREAD(&thr2, 4, 1); /* test queueing after interrupted */

    TEST_CREATE_THREAD(&thr3, 1, 2);

    TEST_INTERRUPT_THREAD(1, thr3); /* test interrupting last waiter */

    gcs_sm_continue (sm);

    pthread_join (thr2, NULL);
    fail_if (global_ret != 0, "global_ret = %ld, expected 0", global_ret);

    /* 3. Unpausing totally interrupted monitor */
    gcs_sm_pause (sm);

    TEST_CREATE_THREAD(&thr1, 1, 0);
    TEST_INTERRUPT_THREAD(1, thr1);

    TEST_CREATE_THREAD(&thr1, 2, 1);
    TEST_INTERRUPT_THREAD(2, thr1);

    gcs_sm_continue (sm);

    /* check that monitor is still functional */
    ret = gcs_sm_enter (sm, &cond, false); // handle inc to 3
    fail_if (ret != 0);

    TEST_CREATE_THREAD(&thr1, 4, 1);

    gcs_sm_leave (sm);
    pthread_join (thr1, NULL);
    fail_if (global_ret != 0, "global_ret = %ld, expected 0", global_ret);

    pthread_cond_destroy (&cond);
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
  tcase_add_test  (tc, gcs_sm_test_interrupt);
  return s;
}

