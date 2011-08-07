// Copyright (C) 2010-2011 Codership Oy <info@codership.com>

// $Id$

/*!
 * @file: package specific part of the main test file.
 */
#ifndef __gcache_tests_hpp__
#define __gcache_tests_hpp__

#define LOG_FILE "gcache_tests.log"

#include "gcache_mem_test.hpp"
#include "gcache_rb_test.hpp"
#include "gcache_page_test.hpp"

extern "C" {
#include <check.h>
}

typedef Suite *(*suite_creator_t)(void);

static suite_creator_t suites[] =
{
    gcache_mem_suite,
    gcache_rb_suite,
    gcache_page_suite,
    0
};

#endif /* __gcache_tests_hpp__ */
