/*
 * Copyright (C) 2014-2020 Codership Oy <info@codership.com>
 */

#include "../src/gu_atomic.hpp"

#include "gu_atomic_test.hpp"

#include "gu_limits.h"
#include <pthread.h>

START_TEST(test_sanity_c)
{
    int64_t i, j, k;

    i = 1; j = 0; k = 3;
    gu_atomic_set(&i, &j); ck_assert(j == 0); ck_assert(i == 0);
    gu_atomic_get(&i, &k); ck_assert(i == 0); ck_assert(k == 0);

    j = gu_atomic_fetch_and_add (&i,  7); ck_assert(j ==  0); ck_assert(i ==  7);
    j = gu_atomic_fetch_and_sub (&i, 10); ck_assert(j ==  7); ck_assert(i == -3);
    j = gu_atomic_fetch_and_or  (&i, 15); ck_assert(j == -3); ck_assert(i == -1);
    j = gu_atomic_fetch_and_and (&i,  5); ck_assert(j == -1); ck_assert(i ==  5);
    j = gu_atomic_fetch_and_xor (&i,  3); ck_assert(j ==  5); ck_assert(i ==  6);
    j = gu_atomic_fetch_and_nand(&i, 15); ck_assert(j ==  6); ck_assert(i == -7);

    j = gu_atomic_add_and_fetch (&i,  7); ck_assert(j ==  0); ck_assert(i ==  0);
    j = gu_atomic_sub_and_fetch (&i, -2); ck_assert(j ==  2); ck_assert(i ==  2);
    j = gu_atomic_or_and_fetch  (&i,  5); ck_assert(j ==  7); ck_assert(i ==  7);
    j = gu_atomic_and_and_fetch (&i, 13); ck_assert(j ==  5); ck_assert(i ==  5);
    j = gu_atomic_xor_and_fetch (&i, 15); ck_assert(j == 10); ck_assert(i == 10);
    j = gu_atomic_nand_and_fetch(&i,  7); ck_assert(j == -3); ck_assert(i == -3);
}
END_TEST

START_TEST(test_sanity_cxx)
{
    gu::Atomic<int64_t> i(1);
    int64_t const k(3);

    ck_assert(i() == 1);
    ck_assert(i() != k);
    ck_assert((i = k) == k);
    ck_assert(i() == k);

    ck_assert(i.fetch_and_zero() == k); ck_assert(i() == 0);
    ck_assert(i.fetch_and_add(5) == 0); ck_assert(i() == 5);
    ck_assert(i.add_and_fetch(3) == 8); ck_assert(i() == 8);
    ck_assert((++i)() == 9); ck_assert(i() == 9);
    ck_assert((--i)() == 8); ck_assert(i() == 8);
    i += 3; ck_assert(i() == 11);
}
END_TEST

// we want it sufficiently long to test above least 4 bytes, but sufficiently
// short to avoid overflow
static long long const increment(333333333333LL);

// number of add/sub thread pairs
static int const n_threads(8);

// maximum iterations number (to guarantee no overflow)
static int const max_iter(GU_LLONG_MAX/increment/n_threads);

// number of iterations capped at 1M, just in case
static int const iterations(max_iter > 1000000 ? 1000000 : max_iter);

static void* add_loop(void* arg)
{
    int64_t* const var(static_cast<int64_t*>(arg));

    for (int i(iterations); --i;)
    {
        gu_atomic_fetch_and_add(var, increment);
    }

    return NULL;
}

static void* sub_loop(void* arg)
{
    int64_t* const var(static_cast<int64_t*>(arg));

    for (int i(iterations); --i;)
    {
        gu_atomic_fetch_and_sub(var, increment);
    }

    return NULL;
}

static int start_threads(pthread_t* threads, int64_t* var)
{
    for (int i(0); i < n_threads; ++i)
    {
        pthread_t* const add_thr(&threads[i * 2]);
        pthread_t* const sub_thr(add_thr + 1);

        int const add_err(pthread_create(add_thr, NULL, add_loop, var));
        int const sub_err(pthread_create(sub_thr, NULL, sub_loop, var));

        if (add_err != 0) return add_err;
        if (sub_err != 0) return sub_err;
    }

    return 0;
}

static int join_threads(pthread_t* threads)
{
    for (int i(0); i < n_threads; ++i)
    {
        pthread_t* const add_thr(&threads[i * 2]);
        pthread_t* const sub_thr(add_thr + 1);

        int const add_err(pthread_join(*add_thr, NULL));
        int const sub_err(pthread_join(*sub_thr, NULL));

        if (add_err != 0) return add_err;
        if (sub_err != 0) return sub_err;
    }

    return 0;
}

// This may not catch concurrency problems every time. But sometimes it should
// (if there are any).
START_TEST(test_concurrency)
{
    ck_assert(iterations >= 1000000);

    int64_t   var(0);
    pthread_t threads[n_threads * 2];

    ck_assert(0 == start_threads(threads, &var));
    ck_assert(0 == join_threads(threads));
    ck_assert(0 == var);
}
END_TEST

Suite* gu_atomic_suite()
{
    TCase* t1 = tcase_create ("sanity");
    tcase_add_test (t1, test_sanity_c);
    tcase_add_test (t1, test_sanity_cxx);

    TCase* t2 = tcase_create ("concurrency");
    tcase_add_test (t2, test_concurrency);
    tcase_set_timeout(t2, 60);

    Suite* s = suite_create ("gu::Atomic");
    suite_add_tcase (s, t1);
    suite_add_tcase (s, t2);

    return s;
}
