// Copyright (C) 2013 Codership Oy <info@codership.com>

// $Id$

#include "gu_alloc_test.hpp"

#include "../src/gu_alloc.hpp"

START_TEST (basic)
{
    const char test0[] = "test0";
    const char test1[] = "test1";

    std::string test_name("gu_alloc_test");
    gu::Allocator a(test_name, sizeof(test1), 1 << 16);
    mark_point();
    void*  p;
    size_t r, s = 0;
    bool   n;

    r = 0; s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (p != 0);
    fail_if (n);
    fail_if (a.size() != s);

    r = sizeof(test0); s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (0 == p);
    fail_if (n);
    fail_if (a.size() != s);
    strcpy (reinterpret_cast<char*>(p), test0);

    /* new page must be allocated */
    r = sizeof(test1); s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (0 == p);
    fail_if (!n);
    fail_if (a.size() != s);
    strcpy (reinterpret_cast<char*>(p), test1);

    r = 0; s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (p != 0);
    fail_if (n);
    fail_if (a.size() != s);

    std::vector<gu::Buf> out;
    out.reserve (a.count());
    mark_point();

    size_t out_size = a.gather (out);

    fail_if (out_size != (sizeof(test0) + sizeof(test1)));
    fail_if (out.size() != 2);
    fail_if (out[0].size != sizeof(test0));
    fail_if (strcmp(reinterpret_cast<const char*>(out[0].ptr), test0));
    fail_if (out[1].size != sizeof(test1));
    fail_if (strcmp(reinterpret_cast<const char*>(out[1].ptr), test1));
}
END_TEST

Suite* gu_alloc_suite ()
{
    TCase* t = tcase_create ("Allocator");
    tcase_add_test (t, basic);

    Suite* s = suite_create ("gu::Allocator");
    suite_add_tcase (s, t);

    return s;
}
