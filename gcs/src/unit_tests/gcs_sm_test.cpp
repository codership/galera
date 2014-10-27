// Copyright (C) 2010-2013 Codership Oy <info@codership.com>

// $Id$


#include "../gcs_sm.hpp"

#include <check.h>
#include <string.h>

#include "gcs_sm_test.hpp"



#define TEST_USLEEP 10000

/* we can't use pthread functions for waiting for certain conditions */
#define WAIT_FOR(cond)                                                  \
    { int count = 1000; while (--count && !(cond)) { usleep (1000); }}

START_TEST (gcs_sm_test_basic)
{
    int ret;

    gcs_sm_t* sm = gcs_sm_create(2, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    int i;
    for (i = 1; i < 5; i++) {
        ret = gcs_sm_enter(sm, &cond, false, true);
        fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
        fail_if(sm->users != 1, "users = %ld, expected 1", sm->users);
        fail_if(sm->entered != 1, "entered = %d, expected 1", sm->entered);

        gcs_sm_leave(sm);
        fail_if(sm->entered != 0, "entered = %d, expected %d", sm->entered, 0);
    }

    ret = gcs_sm_close(sm);
    fail_if(ret);

    gcs_sm_destroy(sm);
    gu_cond_destroy(&cond);
}
END_TEST

volatile long simple_ret;

static void* simple_thread(void* arg)
{
    gcs_sm_t* sm = (gcs_sm_t*) arg;

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    if (0 == (simple_ret = gcs_sm_enter (sm, &cond, false, true))) {
        usleep(1000);
        gcs_sm_leave (sm);
    }

    gu_cond_destroy (&cond);

    return NULL;
}

START_TEST (gcs_sm_test_simple)
{
    int ret;

    gcs_sm_t* sm = gcs_sm_create(4, 1);
    fail_if(!sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    ret = gcs_sm_enter(sm, &cond, false, true);
    fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    fail_if(sm->users != 1, "users = %ld, expected 1", sm->users);
    fail_if(sm->entered != true, "entered = %d, expected %d",
            sm->users, true);

    gu_thread_t t1, t2, t3, t4;

    gu_thread_create (&t1, NULL, simple_thread, sm);
    gu_thread_create (&t2, NULL, simple_thread, sm);
    gu_thread_create (&t3, NULL, simple_thread, sm);

    WAIT_FOR ((long)sm->wait_q_len == sm->users);
    fail_if((long)sm->wait_q_len != sm->users, "wait_q_len = %lu, users = %ld",
            sm->wait_q_len, sm->users);

    gu_thread_create (&t4, NULL, simple_thread, sm);

    mark_point();
    gu_thread_join (t4, NULL); // there's no space in the queue
    fail_if (simple_ret != -EAGAIN);

    fail_if (0 != sm->wait_q_tail, "wait_q_tail = %lu, expected 0",
             sm->wait_q_tail);
    fail_if (1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
             sm->wait_q_head);
    fail_if (4 != sm->users, "users = %lu, expected 4", sm->users);

    gu_info ("Calling gcs_sm_leave()");
    gcs_sm_leave(sm);

    fail_unless(4 > sm->users, "users = %lu, expected 4", sm->users);

    gu_info ("Calling gcs_sm_close()");
    ret = gcs_sm_close(sm);
    fail_if(ret);

    gu_thread_join(t1, NULL);
    gu_thread_join(t2, NULL);
    gu_thread_join(t3, NULL);

    gcs_sm_destroy(sm);
    gu_cond_destroy(&cond);
}
END_TEST


static volatile int order = 0; // global variable to trac the order of events

static void* closing_thread (void* data)
{
    gcs_sm_t* sm = (gcs_sm_t*)data;

    fail_if(order != 0, "order is %d, expected 0", order);

    order = 1;
    int ret = gcs_sm_close(sm);

    fail_if(ret);
    fail_if(order != 2, "order is %d, expected 2", order);

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

    int ret = gcs_sm_enter(sm, &cond, false, true);
    fail_if(ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    fail_if(sm->users != 1, "users = %ld, expected 1", sm->users);
    fail_if(order != 0);

    fail_if(1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
            sm->wait_q_head);
    fail_if(1 != sm->wait_q_tail, "wait_q_tail = %lu, expected 1",
            sm->wait_q_tail);

    gu_thread_t thr;
    gu_thread_create (&thr, NULL, closing_thread, sm);
    WAIT_FOR(1 == order);
    fail_if(order != 1, "order is %d, expected 1", order);
    usleep(TEST_USLEEP); // make sure closing_thread() blocks in gcs_sm_close()

    fail_if(sm->users != 2, "users = %ld, expected 2", sm->users);
    gu_info ("Started close thread, users = %ld", sm->users);

    fail_if(1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
            sm->wait_q_head);
    fail_if(0 != sm->wait_q_tail, "wait_q_tail = %lu, expected 0",
            sm->wait_q_tail);
    fail_if(1 != sm->entered);

    order = 2;
    gcs_sm_leave(sm);

    mark_point();
    gu_thread_join(thr, NULL);

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
    gcs_sm_enter (sm, &cond, true, true);
    gu_info ("pausing_thread entered, pause_order = %d", pause_order);
    fail_if (pause_order != 2, "pause_order = %d, expected 2");
    pause_order = 3;
    usleep(TEST_USLEEP);
    gcs_sm_leave (sm);

    mark_point();
    gu_cond_destroy(&cond);

    gu_info ("pausing_thread exit, pause_order = %d", pause_order);
    return NULL;
}

START_TEST (gcs_sm_test_pause)
{
    int       q_len;
    int       q_len_max;
    int       q_len_min;
    double    q_len_avg;
    long long paused_ns;
    double    paused_avg;

    gcs_sm_t* sm = gcs_sm_create(4, 1);

    fail_if(!sm);
    fail_if(1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
            sm->wait_q_head);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    gu_thread_t thr;

    gcs_sm_stats_get (sm, &q_len, &q_len_max, &q_len_min, &q_len_avg,
                      &paused_ns, &paused_avg);
    fail_if (paused_ns  != 0.0);
    fail_if (paused_avg != 0.0);
    fail_if (q_len_avg  != 0.0);
    fail_if (q_len      != 0);
    fail_if (q_len_max  != 0);
    fail_if (q_len_min  != 0);

    // Test attempt to enter paused monitor
    pause_order = 0;
    gcs_sm_pause (sm);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    WAIT_FOR(1 == pause_order);
    fail_if (pause_order != 1, "pause_order = %d, expected 1");
    usleep(TEST_USLEEP); // make sure pausing_thread blocked in gcs_sm_enter()
    pause_order = 2;

    // testing taking stats in the middle of the pause pt. 1
    gcs_sm_stats_get (sm, &q_len, &q_len_max, &q_len_min, &q_len_avg,
                      &paused_ns, &paused_avg);
    fail_if (paused_ns  <= 0.0);
    fail_if (paused_avg <= 0.0);
    fail_if (q_len_avg  != 0.0);

    gu_info ("Calling gcs_sm_continue()");
    gcs_sm_continue (sm);
    gu_thread_join (thr, NULL);
    fail_if (pause_order != 3, "pause_order = %d, expected 3");

    fail_if(2 != sm->wait_q_head, "wait_q_head = %lu, expected 2",
            sm->wait_q_head);
    fail_if(1 != sm->wait_q_tail, "wait_q_tail = %lu, expected 1",
            sm->wait_q_tail);

    // testing taking stats in the middle of the pause pt. 2
    long long tmp;
    gcs_sm_stats_get (sm, &q_len, &q_len_max, &q_len_min, &q_len_avg,
                      &tmp, &paused_avg);
    fail_if (tmp <= paused_ns); paused_ns = tmp;
    fail_if (paused_avg <= 0.0);
    fail_if (q_len_avg  != 0.0);
    gcs_sm_stats_flush(sm);

    // Testing scheduling capability
    gcs_sm_schedule (sm);
    fail_if(2 != sm->wait_q_tail, "wait_q_tail = %lu, expected 2",
            sm->wait_q_tail);
    gu_thread_create (&thr, NULL, pausing_thread, sm);
    usleep (TEST_USLEEP);
    // no changes in pause_order
    fail_if (pause_order != 3, "pause_order = %d, expected 3");
    pause_order = 0;

    int ret = gcs_sm_enter(sm, &cond, true, true);
    fail_if (ret, "gcs_sm_enter() failed: %d (%s)", ret, strerror(-ret));
    // released monitor lock, thr should continue and schedule,
    // set pause_order to 1
    WAIT_FOR(1 == pause_order);
    fail_if (pause_order != 1, "pause_order = %d, expected 1");
    fail_if (sm->users != 2, "users = %ld, expected 2", sm->users);

    fail_if(2 != sm->wait_q_head, "wait_q_head = %lu, expected 2",
            sm->wait_q_head);
    fail_if(3 != sm->wait_q_tail, "wait_q_tail = %lu, expected 3",
            sm->wait_q_tail);

    gcs_sm_stats_get (sm, &q_len, &q_len_max, &q_len_min, &q_len_avg,
                      &tmp, &paused_avg);
    fail_if (tmp < paused_ns); paused_ns = tmp;
    fail_if (paused_avg != 0.0);
    fail_if (q_len != sm->users, "found q_len %d, expected = %d",
             q_len, sm->users);
    fail_if (q_len_max != q_len, "found q_len_max %d, expected = %d",
             q_len_max, q_len);
    fail_if (q_len_min != 0, "found q_len_min %d, expected = 0",
             q_len_min);
    fail_if ((q_len_avg - 0.5) > 0.0000001 || (q_len_avg - 0.5) < -0.0000001);
    gcs_sm_stats_flush(sm);

    gu_info ("Started pause thread, users = %ld", sm->users);

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
    fail_if (sm->users != 2, "users = %ld, expected 2", sm->users);

    gcs_sm_leave (sm);
    fail_if (sm->users   != 1, "users = %ld, expected 1", sm->users);
    fail_if (sm->entered != 0, "entered = %ld, expected 1", sm->entered);

    fail_if(3 != sm->wait_q_head, "wait_q_head = %lu, expected 3",
            sm->wait_q_head);
    fail_if(3 != sm->wait_q_tail, "wait_q_tail = %lu, expected 3",
            sm->wait_q_tail);

    usleep (TEST_USLEEP); // nothing should change, since monitor is paused
    fail_if (pause_order != 2, "pause_order = %d, expected 2");
    fail_if (sm->entered != 0, "entered = %ld, expected 0", sm->entered);
    fail_if (sm->users   != 1, "users = %ld, expected 1", sm->users);

    gcs_sm_continue (sm); // paused thread should continue
    WAIT_FOR(3 == pause_order);
    fail_if (pause_order != 3, "pause_order = %d, expected 3");

    gcs_sm_stats_get (sm, &q_len, &q_len_max, &q_len_min, &q_len_avg,
                      &tmp, &paused_avg);
    fail_if (tmp <= paused_ns); paused_ns = tmp;
    fail_if (paused_avg <= 0.0);
    fail_if (q_len_avg  != 0.0);

    gcs_sm_enter (sm, &cond, false, true); // by now paused thread exited monitor
    fail_if (sm->entered != 1, "entered = %ld, expected 1", sm->entered);
    fail_if (sm->users   != 1, "users = %ld, expected 1", sm->users);
    fail_if(0 != sm->wait_q_head, "wait_q_head = %lu, expected 0",
            sm->wait_q_head);
    fail_if(0 != sm->wait_q_tail, "wait_q_tail = %lu, expected 0",
            sm->wait_q_tail);

    gcs_sm_leave (sm);
    fail_if(1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
            sm->wait_q_head);

    mark_point();
    gu_cond_destroy(&cond);
    gcs_sm_close (sm);

    mark_point();
    gu_thread_join(thr, NULL);

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

        if (0 == (global_ret = gcs_sm_enter (sm, &cond, true, true))) {
            gcs_sm_leave (sm);
        }
        pthread_cond_destroy (&cond);
    }

    return NULL;
}

#define TEST_CREATE_THREAD(thr, tail, h, u)                             \
    global_handle = -1;                                                 \
    gu_thread_create (thr, NULL, interrupt_thread, sm);                 \
    WAIT_FOR(global_handle == h);                                       \
    fail_if (sm->wait_q_tail != tail, "wait_q_tail = %lu, expected %lu", \
             sm->wait_q_tail, tail);                                    \
    fail_if (global_handle != h, "global_handle = %ld, expected %ld",   \
             global_handle, h);                                         \
    fail_if (sm->users != u, "users = %ld, expected %ld", sm->users, u);

#define TEST_INTERRUPT_THREAD(h, t)                                     \
    ret = gcs_sm_interrupt (sm, (h));                                   \
    fail_if (ret != 0);                                                 \
    gu_thread_join ((t), NULL);                                         \
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
    fail_if (sm->wait_q_tail != 1, "wait_q_tail = %lu, expected 1",
             sm->wait_q_tail);

    long ret = gcs_sm_enter (sm, &cond, true, true);
    fail_if (ret != 0);

    /* 1. Test interrupting blocked by previous thread */
    TEST_CREATE_THREAD(&thr1, 2, 3, 2);

    TEST_CREATE_THREAD(&thr2, 3, 4, 3);

    TEST_INTERRUPT_THREAD(3, thr1);

    gcs_sm_leave (sm); // this should let 2nd enter monitor
    gu_thread_join (thr2, NULL);
    fail_if (global_ret != 0, "global_ret = %ld, expected 0", global_ret);
    fail_if (sm->users  != 0, "users = %ld, expected 0", sm->users);

    ret = gcs_sm_interrupt (sm, 4); // try to interrupt 2nd which has exited
    fail_if (ret != -ESRCH);

    /* 2. Test interrupting blocked by pause */
    gcs_sm_pause (sm);

    TEST_CREATE_THREAD(&thr1, 0, 1, 1);

    TEST_INTERRUPT_THREAD(1, thr1);

    TEST_CREATE_THREAD(&thr2, 1, 2, 2); /* test queueing after interrupted */

    TEST_CREATE_THREAD(&thr3, 2, 3, 3);

    TEST_INTERRUPT_THREAD(3, thr3); /* test interrupting last waiter */

    gcs_sm_continue (sm);

    gu_thread_join (thr2, NULL);
    fail_if (global_ret != 0, "global_ret = %ld, expected 0", global_ret);

    /* 3. Unpausing totally interrupted monitor */
    gcs_sm_pause (sm);

    TEST_CREATE_THREAD(&thr1, 3, 4, 1);
    TEST_INTERRUPT_THREAD(4, thr1);

    TEST_CREATE_THREAD(&thr1, 0, 1, 2);
    TEST_INTERRUPT_THREAD(1, thr1);

    gcs_sm_continue (sm);

    /* check that monitor is still functional */
    ret = gcs_sm_enter (sm, &cond, false, true);
    fail_if (ret != 0);

    fail_if(1 != sm->wait_q_head, "wait_q_head = %lu, expected 1",
            sm->wait_q_head);
    fail_if(1 != sm->wait_q_tail, "wait_q_tail = %lu, expected 1",
            sm->wait_q_tail);
    fail_if (sm->users != 1, "users = %ld, expected 1", sm->users);

    TEST_CREATE_THREAD(&thr1, 2, 3, 2);

    gu_info ("Calling gcs_sm_leave()");
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
  tcase_add_test  (tc, gcs_sm_test_simple);
  tcase_add_test  (tc, gcs_sm_test_close);
  tcase_add_test  (tc, gcs_sm_test_pause);
  tcase_add_test  (tc, gcs_sm_test_interrupt);
  return s;
}

