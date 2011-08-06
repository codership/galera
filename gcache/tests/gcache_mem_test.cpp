/*
 * Copyright (C) 2011 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcache_mem_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_mem_test.hpp"

START_TEST(test1)
{
    ssize_t const bh_size = sizeof(gcache::BufferHeader);
}
END_TEST

Suite* gcache_mem_suite()
{
    Suite* s = suite_create("gcache::MemStore");
    TCase* tc;

    tc = tcase_create("test");
    tcase_add_test(tc, test1);
    suite_add_tcase(s, tc);

    return s;
}
