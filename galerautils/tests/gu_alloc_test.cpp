// Copyright (C) 2013-2020 Codership Oy <info@codership.com>

// $Id$

#include "../src/gu_alloc.hpp"

#include "gu_alloc_test.hpp"

class TestBaseName : public gu::Allocator::BaseName
{
    std::string str_;

public:

    TestBaseName(const char* name) : str_(name) {}
    void print(std::ostream& os) const { os << str_; }
};

START_TEST (basic)
{
    ssize_t const extra_size(1 << 12); /* extra size to force new page */
    size_t reserved[extra_size / sizeof(size_t)]; /* size_t for alignment */

    const char test0[] = "test0";
    ssize_t const test0_size(sizeof(test0));

    const char test1[] = "test1";
    ssize_t const test1_size(sizeof(test1) + extra_size);

    TestBaseName test_name("gu_alloc_test");
    gu::Allocator a(test_name, reserved, sizeof(reserved),
                    sizeof(test1), 1 << 16);
    mark_point();
    void*  p;
    size_t r, s = 0;
    bool   n;

    r = 0; s += r;
    mark_point();
    p = a.alloc(r, n);
    ck_assert(0 == p);
    ck_assert(!n);
    ck_assert(a.size() == s);

    r = test0_size; s += r;
    mark_point();
    p = a.alloc(r, n);
    ck_assert(0 != p);
    ck_assert(!n);
    ck_assert(a.size() == s);
    strcpy (reinterpret_cast<char*>(p), test0);

    r = test1_size; s += r;
    mark_point();
    p = a.alloc(r, n);
    ck_assert(0 != p);
    ck_assert(n);                            /* new page must be allocated */
    ck_assert(a.size() == s);
    strcpy (reinterpret_cast<char*>(p), test1);

    r = 0; s += r;
    mark_point();
    p = a.alloc(r, n);
    ck_assert(0 == p);
    ck_assert(!n);
    ck_assert(a.size() == s);

#ifdef GU_ALLOCATOR_DEBUG
    std::vector<gu::Buf> out;
    out.reserve (a.count());
    mark_point();

    size_t out_size = a.gather (out);

    ck_assert(out_size == test0_size + test1_size);
    ck_assert(out.size() == 2);
    ck_assert(out[0].size == test0_size);
    ck_assert(!strcmp(reinterpret_cast<const char*>(out[0].ptr), test0));
    ck_assert(out[1].size == test1_size);
    ck_assert(!strcmp(reinterpret_cast<const char*>(out[1].ptr), test1));
#endif /* GU_ALLOCATOR_DEBUG */
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
