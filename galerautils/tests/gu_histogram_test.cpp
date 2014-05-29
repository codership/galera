/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#include "../src/gu_histogram.hpp"
#include "../src/gu_logger.hpp"
#include <cstdlib>

#include "gu_histogram_test.hpp"

using namespace gu;

START_TEST(test_histogram)
{

    Histogram hs("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.");

    hs.insert(0.001);
    log_info << hs;

    for (size_t i = 0; i < 1000; ++i)
    {
        hs.insert(double(::rand())/RAND_MAX);
    }

    log_info << hs;

    hs.clear();

    log_info << hs;
}
END_TEST

Suite* gu_histogram_suite()
{
    TCase* t = tcase_create ("test_histogram");
    tcase_add_test (t, test_histogram);

    Suite* s = suite_create ("gu::Histogram");
    suite_add_tcase (s, t);

    return s;
}
