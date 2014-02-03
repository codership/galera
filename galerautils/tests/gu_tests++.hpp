// Copyright (C) 2009-2014 Codership Oy <info@codership.com>

// $Id$

/*!
 * @file: package specific part of the main test file.
 */
#ifndef __gu_testspp_hpp__
#define __gu_testspp_hpp__

#define LOG_FILE "gu_tests++.log"

#include "gu_string_test.hpp"
#include "gu_uri_test.hpp"
#include "gu_config_test.hpp"
#include "gu_net_test.hpp"
#include "gu_datetime_test.hpp"
#include "gu_vlq_test.hpp"

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
{
    gu_string_suite,
    gu_uri_suite,
    gu_config_suite,
    gu_net_suite,
    gu_datetime_suite,
    gu_vlq_suite,
    0
};

#endif /* __gu_testspp_hpp__ */
