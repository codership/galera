/* Copyright (C) 2013 Codership Oy <info@codership.com>
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
    fail_if (v1->size() != 0);
    v1->push_back(12);
    fail_if (v1->size() != 1);
    v1->resize(11);
    fail_if (v1->size() != 11);
    fail_if (v1.in_heap() != false);
    v1[10]=1;
    fail_if (v1[10] != v1()[10]);

    gu::Vector<int, 16> v2(v1);
    fail_if (v2->size() != v1->size());
    fail_if (v1[10] != v2[10]);
    fail_if (&v1[10] == &v2[10]);
    v2[10]=2;
    fail_if (v1[10] == v2[10]);
    v2() = v1();
    fail_if (v1[10] != v2[10]);
    fail_if (&v1[0] == &v2[0]);
    fail_if (v2.in_heap() != false);

    v2->resize(32);
    fail_if (v2.in_heap() != true);
    fail_if (v1.in_heap() != false);
    v2[25]=1;
    v1->resize(32);
    fail_if (v1.in_heap() != true);
    v1[25]=2;
    fail_if (v1[25] == v2[25]);
}
END_TEST

START_TEST(size_test)
{
    gu::Vector<void*, 1> v;
    fail_if (v.size() != 0);

    void* const ptr1(reinterpret_cast<void*>(1));
    v.push_back(ptr1);
    fail_if(v()[0] != ptr1);
    fail_if(v[0] != ptr1);
    fail_if(v().front() != ptr1);
    fail_if(v.front() != ptr1);
    fail_if(v().back() != ptr1);
    fail_if(v.back() != ptr1);
    fail_if(v.size() != 1, "v.size() expected 1, got %llu", v.size());

    void* const ptr2(reinterpret_cast<void*>(2));
    v.push_back(ptr2);
    fail_if(v()[0] != ptr1);
    fail_if(v[0] != ptr1);
    fail_if(v()[1] != ptr2);
    fail_if(v[1] != ptr2);
    fail_if(v().front() != ptr1);
    fail_if(v.front() != ptr1);
    fail_if(v().back() != ptr2);
    fail_if(v.back() != ptr2);
    fail_if(v.size() != 2, "v.size() expected 2, got %llu", v.size());
    fail_if(!v.in_heap());
}
END_TEST

Suite*
gu_vector_suite(void)
{
    TCase* t = tcase_create ("simple_test");
    tcase_add_test (t, simple_test);
    tcase_add_test (t, size_test);

    Suite* s = suite_create ("gu::Vector");
    suite_add_tcase (s, t);

    return s;
}

