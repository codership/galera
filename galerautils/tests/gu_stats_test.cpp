/*
 * Copyright (C) 2014-2020 Codership Oy <info@codership.com>
 */

#include "../src/gu_stats.hpp"

#include "gu_stats_test.hpp"

#include <cmath>
#include <limits>

using namespace gu;

static inline bool double_equal(double a, double b)
{
    return (std::fabs(a - b) <=
            std::fabs(a + b) * std::numeric_limits<double>::epsilon());
}

START_TEST(test_stats)
{
    Stats st;
    st.insert(10.0);
    st.insert(20.0);
    st.insert(30.0);
    ck_assert(double_equal(st.mean(), 20.0));
    ck_assert_msg(double_equal(st.variance() * 3, 200.0),
                  "%e != 0", st.variance()*3-200.0);
    ck_assert(double_equal(st.min(), 10.0));
    ck_assert(double_equal(st.max(), 30.0));
    st.clear();

    st.insert(10.0);
    ck_assert(double_equal(st.mean(), 10.0));
    ck_assert(double_equal(st.variance(), 0.0));
    ck_assert(double_equal(st.min(), 10.0));
    ck_assert(double_equal(st.max(), 10.0));
    st.clear();

    ck_assert(double_equal(st.mean(), 0.0));
    ck_assert(double_equal(st.variance(), 0.0));
    ck_assert(double_equal(st.min(), 0.0));
    ck_assert(double_equal(st.max(), 0.0));
}
END_TEST

Suite* gu_stats_suite()
{
    TCase* t = tcase_create ("test_stats");
    tcase_add_test (t, test_stats);

    Suite* s = suite_create ("gu::Stats");
    suite_add_tcase (s, t);

    return s;
}
