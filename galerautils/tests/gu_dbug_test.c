// Copyright (C) 2008-2017 Codership Oy <info@codership.com>

// $Id$

/* Pthread yield */
#define _GNU_SOURCE 1
#include <sched.h>
#include <check.h>
#include <time.h>
#include "gu_dbug_test.h"
#include "../src/gu_dbug.h"
#include "../src/gu_threads.h"

static void cf()
{
    GU_DBUG_ENTER("cf");
    GU_DBUG_PRINT("galera", ("hello from cf"));
    sched_yield();
    GU_DBUG_VOID_RETURN;
}

static void bf()
{
    GU_DBUG_ENTER("bf");
    GU_DBUG_PRINT("galera", ("hello from bf"));
    sched_yield();
    cf();
    GU_DBUG_VOID_RETURN;
}

static void af()
{
    GU_DBUG_ENTER("af");
    GU_DBUG_PRINT("galera", ("hello from af"));
    sched_yield();
    bf();
    GU_DBUG_VOID_RETURN;
}

static time_t stop = 0;

static void *dbg_thr(void *arg)
{
    while (time(NULL) < stop) { af(); }
    gu_thread_exit(NULL);
}

START_TEST(gu_dbug_test)
{
    int i;
#define N_THREADS 10
    gu_thread_t th[N_THREADS];

    /* Log > /dev/null */
    GU_DBUG_FILE = fopen("/dev/null", "a+");

    /* These should not produce output yet */
    af();
    af();
    af();

    /* Start logging */
    GU_DBUG_PUSH("d:t:i");
    GU_DBUG_PRINT("galera", ("Start logging"));
    af();
    af();
    af();

    /* Run few threads concurrently */
    stop = time(NULL) + 2;
    for (i = 0; i < N_THREADS; i++)
        gu_thread_create(&th[i], NULL, &dbg_thr, NULL);
    for (i = 0; i < N_THREADS; i++)
        gu_thread_join(th[i], NULL);
}
END_TEST

Suite *gu_dbug_suite(void)
{
  Suite *s  = suite_create("Galera dbug functions");
  TCase *tc = tcase_create("gu_dbug");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_dbug_test);
  tcase_set_timeout(tc, 60);
  return s;
}
