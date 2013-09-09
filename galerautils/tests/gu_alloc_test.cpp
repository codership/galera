// Copyright (C) 2013 Codership Oy <info@codership.com>

// $Id$

#include "../src/gu_alloc.hpp"

#include "gu_alloc_test.hpp"

START_TEST (basic)
{
    ssize_t const extra_size(1 << 12); /* extra size to force new page */
    gu::byte_t reserved[extra_size];

    const char test0[] = "test0";
    ssize_t const test0_size(sizeof(test0));

    const char test1[] = "test1";
    ssize_t const test1_size(sizeof(test1) + extra_size);

    gu::String<> test_name("gu_alloc_test");
    gu::Allocator a(reserved, sizeof(reserved),
                    test_name, sizeof(test1), 1 << 16);
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

    r = test0_size; s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (0 == p);
    fail_if (n);
    fail_if (a.size() != s);
    strcpy (reinterpret_cast<char*>(p), test0);

    r = test1_size; s += r;
    mark_point();
    p = a.alloc(r, n);
    fail_if (0 == p);
    fail_if (!n);                            /* new page must be allocated */
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

    fail_if (out_size != test0_size + test1_size);
    fail_if (out.size() != 2);
    fail_if (out[0].size != test0_size);
    fail_if (strcmp(reinterpret_cast<const char*>(out[0].ptr), test0));
    fail_if (out[1].size != test1_size);
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
