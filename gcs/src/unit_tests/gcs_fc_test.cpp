// Copyright (C) 2010-2020 Codership Oy <info@codership.com>

// $Id$

#include "gcs_fc_test.hpp"
#include "../gcs_fc.hpp"

#include <stdbool.h>
#include <string.h>

START_TEST(gcs_fc_test_limits)
{
    gcs_fc_t fc;
    int      ret;

    ret = gcs_fc_init (&fc, 16, 0.5, 0.1);
    ck_assert(ret == 0);

    ret = gcs_fc_init (&fc, -1, 0.5, 0.1);
    ck_assert(ret == -EINVAL);

    ret = gcs_fc_init (&fc, 16, 1.0, 0.1);
    ck_assert(ret == -EINVAL);

    ret = gcs_fc_init (&fc, 16, 0.5, 1.0);
    ck_assert(ret == -EINVAL);
}
END_TEST

/* This is a macro to preserve line numbers in ck_assert_msg() output */
#define SKIP_N_ACTIONS(fc_,n_)                                          \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < n_; ++i)                                        \
        {                                                               \
            long long ret = gcs_fc_process (fc_, 0);                    \
            ck_assert_msg(ret == 0, "0-sized action #%d returned %lld (%s)", \
                          i, ret, strerror(-ret));                      \
        }                                                               \
    }

START_TEST(gcs_fc_test_basic)
{
    gcs_fc_t  fc;
    int       ret;
    long long pause;

    ret = gcs_fc_init (&fc, 16, 0.5, 0.1);
    ck_assert(ret == 0);

    gcs_fc_reset (&fc, 8);
    usleep (1000);
    SKIP_N_ACTIONS(&fc, 7);

    /* Here we exceed soft limit almost instantly, which should give a very high
     * data rate and as a result a need to sleep */
    pause = gcs_fc_process (&fc, 7);
    ck_assert_msg(pause > 0, "Soft limit trip returned %lld (%s)",
                  pause, strerror(-pause));

    gcs_fc_reset (&fc, 7);
    usleep (1000);
    SKIP_N_ACTIONS(&fc, 7);

    /* Here we reach soft limit almost instantly, which should give a very high
     * data rate, but soft limit is not exceeded, so no sleep yet. */
    pause = gcs_fc_process (&fc, 1);
    ck_assert_msg(pause == 0, "Soft limit touch returned %lld (%s)",
                  pause, strerror(-pause));

    SKIP_N_ACTIONS(&fc, 7);
    usleep (1000);
    pause = gcs_fc_process (&fc, 7);
    ck_assert_msg(pause > 0, "Soft limit trip returned %lld (%s)",
                  pause, strerror(-pause));

    /* hard limit excess should be detected instantly */
    pause = gcs_fc_process (&fc, 1);
    ck_assert_msg(pause == -ENOMEM, "Hard limit trip returned %lld (%s)",
                  pause, strerror(-pause));
}
END_TEST

static inline bool
double_equals (double a, double b)
{
    static double const eps = 0.001;
    double diff = (a - b) / (a + b); // roughly relative difference
    return !(diff > eps || diff < -eps);
}

START_TEST(gcs_fc_test_precise)
{
    gcs_fc_t fc;
    long long       ret;
    struct timespec p10ms = {0, 10000000 }; // 10 ms

    ret = gcs_fc_init (&fc, 2000, 0.5, 0.5);
    ck_assert(ret == 0);

    gcs_fc_reset (&fc, 500);
    SKIP_N_ACTIONS(&fc, 7);

    nanosleep (&p10ms, NULL);
    ret = gcs_fc_process (&fc, 1000);
    ck_assert_msg(ret > 0, "Soft limit trip returned %lld (%s)",
                  ret, strerror(-ret));

    // measured data rate should be ~100000 b/s
    // slave queue length should be half-way between soft limit and hard limit
    // desired rate should be half between 1.0 and 0.5 of full rate -> 75000 b/s
    // excess over soft limit is 500 and corresponding interval: 5ms
    // (500/5ms == 100000 b/s)
    // additional sleep must be 1.6667 ms (500/(5 + 1.6667) ~ 75000 b/s)

    double const correction = 100000.0/fc.max_rate; // due to imprecise sleep
    double const expected_sleep = 0.001666667*correction;
    double sleep = ((double)ret)*1.0e-9;
    ck_assert_msg(double_equals(sleep, expected_sleep),
                  "Sleep: %f, expected %f", sleep, expected_sleep);
}
END_TEST

Suite *gcs_fc_suite(void)
{
    Suite *s  = suite_create("GCS state transfer FC");
    TCase *tc = tcase_create("gcs_fc");

    suite_add_tcase (s, tc);
    tcase_add_test  (tc, gcs_fc_test_limits);
    tcase_add_test  (tc, gcs_fc_test_basic);
    tcase_add_test  (tc, gcs_fc_test_precise);

    return s;
}
