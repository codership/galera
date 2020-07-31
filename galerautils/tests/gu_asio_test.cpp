/*
 * Copyright (C) 2019-2020 Codership Oy <info@codership.com>
 */


#include "gu_asio.hpp"
#include "gu_asio_test.hpp"

START_TEST(test_make_address_v4)
{
    asio::ip::address a(gu::make_address("10.2.14.1"));
    ck_assert(a.is_v4());
    ck_assert(a.is_v6() == false);
}
END_TEST

// Verify that link local address without scope ID is parsed
// properly.
START_TEST(test_make_address_v6_link_local)
{
    asio::ip::address a(gu::make_address("fe80::fc87:f2ff:fe85:6ba6"));
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 0);
    ck_assert(a.to_v6().is_link_local());

    a = gu::make_address("[fe80::fc87:f2ff:fe85:6ba6]");
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 0);
    ck_assert(a.to_v6().is_link_local());
}
END_TEST

// Verify that link local address with scope ID is parsed
// properly.
START_TEST(test_make_address_v6_link_local_with_scope_id)
{
    asio::ip::address a(gu::make_address("fe80::fc87:f2ff:fe85:6ba6%1"));
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 1);

    a = gu::make_address("[fe80::fc87:f2ff:fe85:6ba6%1]");
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 1);
}
END_TEST

Suite* gu_asio_suite()
{
    Suite* s(suite_create("gu::asio"));
    TCase* tc;

    tc = tcase_create("test_make_address_v4");
    tcase_add_test(tc, test_make_address_v4);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_make_address_v6_link_local");
    tcase_add_test(tc, test_make_address_v6_link_local);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_make_address_v6_link_local_with_scope_id");
    tcase_add_test(tc, test_make_address_v6_link_local_with_scope_id);
    suite_add_tcase(s, tc);

    return s;
}

