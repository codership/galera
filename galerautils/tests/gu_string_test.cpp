// Copyright (C) 2009 Codership Oy <info@codership.com>

#include "gu_string.hpp"
#include "gu_string_test.hpp"

using std::string;
using std::vector;

START_TEST(test_strsplit)
{
    string str = "foo bar baz";
    vector<string> vec = gu::strsplit(str, ' ');
    fail_unless(vec.size() == 3);
    fail_unless(vec[0] == "foo");
    fail_unless(vec[1] == "bar");
    fail_unless(vec[2] == "baz");

}
END_TEST

Suite* gu_string_suite(void)
{
    Suite* s = suite_create("galerautils++ String");
    TCase* tc;

    tc = tcase_create("strsplit");
    tcase_add_test(tc, test_strsplit);
    suite_add_tcase(s, tc);
    return s;
}
