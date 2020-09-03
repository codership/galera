/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <unistd.h> // usleep()
#include <string.h> // strerror()

#include "../src/gu_log.h"
#include "../src/gu_lock_step.h"
#include "gu_lock_step_test.h"

#define TEST_USLEEP 1000 // 1ms
#define WAIT_FOR(cond)                                                  \
    { int count = 1000; while (--count && !(cond)) { usleep (TEST_USLEEP); }}

gu_lock_step_t LS;

static void*
lock_step_thread (void* arg)
{
    gu_lock_step_wait (&LS);

    return NULL;
}

START_TEST (gu_lock_step_test)
{
    const long  timeout = 500; // 500 ms
    long        ret;
    gu_thread_t thr1, thr2;

    gu_lock_step_init (&LS);
    ck_assert(LS.wait    == 0);
    ck_assert(LS.enabled == false);

    // first try with lock-stepping disabled
    ret = gu_thread_create (&thr1, NULL, lock_step_thread, NULL);
    ck_assert(ret == 0);
    WAIT_FOR(0 == LS.wait); // 10ms
    ck_assert(LS.wait == 0); // by default lock-step is disabled

    ret = gu_thread_join (thr1, NULL);
    ck_assert_msg(ret == 0,
                  "gu_thread_join() failed: %ld (%s)", ret, strerror(ret));

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(-1 == ret);

    // enable lock-step
    gu_lock_step_enable (&LS, true);
    ck_assert(LS.enabled == true);

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(0 == ret); // nobody's waiting

    ret = gu_thread_create (&thr1, NULL, lock_step_thread, NULL);
    ck_assert(ret == 0);
    WAIT_FOR(1 == LS.wait); // 10ms
    ck_assert(LS.wait == 1);

    ret = gu_thread_create (&thr2, NULL, lock_step_thread, NULL);
    ck_assert(ret == 0);
    WAIT_FOR(2 == LS.wait); // 10ms
    ck_assert(LS.wait == 2);

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(ret == 2);     // there were 2 waiters
    ck_assert(LS.wait == 1); // 1 waiter remains

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(ret == 1); 
    ck_assert(LS.wait == 0); // 0 waiters remain

    ret = gu_thread_join (thr1, NULL);
    ck_assert_msg(ret == 0,
                  "gu_thread_join() failed: %ld (%s)", ret, strerror(ret));
    ret = gu_thread_join (thr2, NULL);
    ck_assert_msg(ret == 0,
                  "gu_thread_join() failed: %ld (%s)", ret, strerror(ret));

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(ret == 0);     // there were 0 waiters 
    ck_assert_msg(LS.wait == 0,
                  "Expected LS.wait to be 0, found: %ld", LS.wait);

    gu_lock_step_destroy (&LS);
}
END_TEST

#define RACE_ITERATIONS 1000

static void*
lock_step_race (void* arg)
{
    long i;

    for (i = 0; i < RACE_ITERATIONS; i++)
        gu_lock_step_wait (&LS);

    return NULL;
}

START_TEST (gu_lock_step_race)
{
    const long  timeout = 500; // 500 ms
    long        ret, i;
    gu_thread_t thr1;

    gu_lock_step_init   (&LS);
    gu_lock_step_enable (&LS, true);
    ck_assert(LS.enabled == true);

    ret = gu_thread_create (&thr1, NULL, lock_step_race, NULL);
    ck_assert(ret == 0);

    for (i = 0; i < RACE_ITERATIONS; i++) {
        ret = gu_lock_step_cont (&LS, timeout);
        ck_assert_msg(ret == 1, "No waiter at iteration: %ld", i);
    }
    ck_assert(LS.wait == 0); // 0 waiters remain

    ret = gu_thread_join (thr1, NULL);
    ck_assert_msg(ret == 0,
                  "gu_thread_join() failed: %ld (%s)", ret, strerror(ret));

    ret = gu_lock_step_cont (&LS, timeout);
    ck_assert(ret == 0);
}
END_TEST

Suite *gu_lock_step_suite(void)
{
  Suite *suite = suite_create("Galera LOCK_STEP utils");
  TCase *tcase = tcase_create("gu_lock_step");

  suite_add_tcase (suite, tcase);
  tcase_add_test  (tcase, gu_lock_step_test);
  tcase_add_test  (tcase, gu_lock_step_race);
  return suite;
}

