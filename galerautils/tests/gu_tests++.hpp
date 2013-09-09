// Copyright (C) 2009-2013 Codership Oy <info@codership.com>

// $Id$

/*!
 * @file: package specific part of the main test file.
 */
#ifndef __gu_testspp_hpp__
#define __gu_testspp_hpp__

#define LOG_FILE "gu_tests++.log"

#include "gu_vector_test.hpp"
#include "gu_string_test.hpp"
#include "gu_digest_test.hpp"
#include "gu_vlq_test.hpp"
#include "gu_alloc_test.hpp"
#include "gu_rset_test.hpp"
#include "gu_string_utils_test.hpp"
#include "gu_uri_test.hpp"
#include "gu_net_test.hpp"
#include "gu_datetime_test.hpp"

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
{
    gu_vector_suite,
    gu_string_suite,
    gu_vlq_suite,
    gu_digest_suite,
    gu_alloc_suite,
    gu_rset_suite,
    gu_string_utils_suite,
    gu_uri_suite,
    gu_net_suite,
    gu_datetime_suite,
    0
};

#endif /* __gu_testspp_hpp__ */
