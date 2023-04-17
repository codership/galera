/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

#include "gu_utils.hpp"
#include "gu_utils_test++.hpp"

static void assert_invalid_int(const std::string& test_str,
                               std::ios_base& (*f)(std::ios_base&) = std::dec)
{
    bool exception(false);
    try
    {
        gu::from_string<int>(test_str);
    }
    catch (gu::NotFound)
    {
        exception = true;
    }
    ck_assert(exception);
}

START_TEST(test_from_string_invalid_int)
{
    // used to parse '1'
    assert_invalid_int("1dummy");
    assert_invalid_int("1 dummy");
    assert_invalid_int("0x1whatever", std::hex);

    // used to parse 'd'
    assert_invalid_int("dummy", std::hex);
}
END_TEST


static void assert_invalid_bool(const std::string& test_str)
{
    bool exception(false);
    try
    {
        gu::from_string<bool>(test_str);
    }
    catch (gu::NotFound)
    {
        exception = true;
    }
    ck_assert(exception);
}

START_TEST(test_from_string_invalid_bool)
{
    assert_invalid_bool("true 1");
}
END_TEST


Suite* gu_utils_cpp_suite()
{
    Suite* s = suite_create("gu::utils");
    TCase* tc;

    tc = tcase_create("test_from_string_invalid_int");
    tcase_add_test(tc, test_from_string_invalid_int);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_from_string_invalid_bool");
    tcase_add_test(tc, test_from_string_invalid_bool);
    suite_add_tcase(s, tc);

    return s;
}
