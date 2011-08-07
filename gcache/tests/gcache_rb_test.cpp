/*
 * Copyright (C) 2011 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "gcache_rb_store.hpp"
#include "gcache_bh.hpp"
#include "gcache_rb_test.hpp"

START_TEST(test1)
{
//    const char* const dir_name = "";
//    ssize_t const bh_size = sizeof(gcache::BufferHeader);
}
END_TEST

Suite* gcache_rb_suite()
{
    Suite* s = suite_create("gcache::RbStore");
    TCase* tc;

    tc = tcase_create("test");
    tcase_add_test(tc, test1);
    suite_add_tcase(s, tc);

    return s;
}
