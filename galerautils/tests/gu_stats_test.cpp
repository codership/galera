/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#include "../src/gu_stats.hpp"

#include "gu_stats_test.hpp"

#include <cmath>
#include <limits>

using namespace gu;

#define double_equal(a,b) \
    (std::fabs((a) - (b)) < std::numeric_limits<double>::epsilon())

START_TEST(test_stats)
{
    Stats st;
    st.insert(10.0);
    st.insert(20.0);
    st.insert(30.0);
    fail_if(!double_equal(st.mean(), 20.0));
    fail_if(!double_equal(st.variance() * 3, 200.0));
    st.clear();

    st.insert(10.0);
    fail_if(!double_equal(st.mean(), 10.0));
    fail_if(!double_equal(st.variance(), 0.0));
    st.clear();
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
