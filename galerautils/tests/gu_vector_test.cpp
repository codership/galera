/* Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../src/gu_vector.hpp"

#include "gu_vector_test.hpp"

START_TEST (simple_test)
{
    // we are not to test the whole vector functionality, it is provided
    // by incorporated std::vector. We just need to see that allocator
    // works as expected

    gu::Vector<int, 16> v1;
    v1->reserve(12);
    ck_assert(v1->size() == 0);
    v1->push_back(12);
    ck_assert(v1->size() == 1);
    v1->resize(11);
    ck_assert(v1->size() == 11);
    ck_assert(v1.in_heap() == false);
    v1[10]=1;
    ck_assert(v1[10] == v1()[10]);

    gu::Vector<int, 16> v2(v1);
    ck_assert(v2->size() == v1->size());
    ck_assert(v1[10] == v2[10]);
    ck_assert(&v1[10] != &v2[10]);
    v2[10]=2;
    ck_assert(v1[10] != v2[10]);
    v2() = v1();
    ck_assert(v1[10] == v2[10]);
    ck_assert(&v1[0] != &v2[0]);
    ck_assert(v2.in_heap() == false);

    v2->resize(32);
    ck_assert(v2.in_heap() == true);
    ck_assert(v1.in_heap() == false);
    v2[25]=1;
    v1->resize(32);
    ck_assert(v1.in_heap() == true);
    v1[25]=2;
    ck_assert(v1[25] != v2[25]);
}
END_TEST

Suite*
gu_vector_suite(void)
{
    TCase* t = tcase_create ("simple_test");
    tcase_add_test (t, simple_test);

    Suite* s = suite_create ("gu::Vector");
    suite_add_tcase (s, t);

    return s;
}

