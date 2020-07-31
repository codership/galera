// Copyright (C) 2009-2020 Codership Oy <info@codership.com>

#include "gu_string_utils.hpp"
#include "gu_string_utils_test.hpp"

using std::string;
using std::vector;

START_TEST(test_strsplit)
{
    string str = "foo bar baz";
    vector<string> vec = gu::strsplit(str, ' ');
    ck_assert(vec.size() == 3);
    ck_assert(vec[0] == "foo");
    ck_assert(vec[1] == "bar");
    ck_assert(vec[2] == "baz");
}
END_TEST

START_TEST(test_tokenize)
{
    vector<string> vec = gu::tokenize("", 'a', 'b', false);
    ck_assert(vec.size() == 0);

    vec = gu::tokenize("", 'a', 'b', true);
    ck_assert(vec.size() == 1);
    ck_assert(vec[0] == "");

    vec = gu::tokenize("a", 'a', 'b', false);
    ck_assert(vec.size() == 0);

    vec = gu::tokenize("a", 'a', 'b', true);
    ck_assert(vec.size() == 2);
    ck_assert(vec[0] == "");
    ck_assert(vec[1] == "");

    vec = gu::tokenize("foo bar baz");
    ck_assert(vec.size() == 3);
    ck_assert(vec[0] == "foo");
    ck_assert(vec[1] == "bar");
    ck_assert(vec[2] == "baz");

    vec = gu::tokenize("foo\\ bar baz");
    ck_assert(vec.size() == 2);
    ck_assert_msg(vec[0] == "foo bar", "expected 'foo bar', found '%s'",
                  vec[0].c_str());
    ck_assert(vec[1] == "baz");

    vec = gu::tokenize("foo\\;;bar;;baz;", ';', '\\', false);
    ck_assert(vec.size() == 3);
    ck_assert(vec[0] == "foo;");
    ck_assert(vec[1] == "bar");
    ck_assert(vec[2] == "baz");

    vec = gu::tokenize("foo\\;;bar;;baz;", ';', '\\', true);
    ck_assert_msg(vec.size() == 5, "vetor length %zu, expected 5", vec.size());
    ck_assert(vec[0] == "foo;");
    ck_assert(vec[1] == "bar");
    ck_assert(vec[2] == "");
    ck_assert(vec[3] == "baz");
    ck_assert(vec[4] == "");
}
END_TEST

START_TEST(test_trim)
{
    string full1 = ".,wklerf joweji";
    string full2 = full1;

    gu::trim (full2);
    ck_assert(full1 == full2);

    string part = " part ";

    gu::trim (part);
    ck_assert(part.length() == 4);
    ck_assert(0 == part.compare("part"));

    string empty;

    gu::trim (empty);
    ck_assert(empty.empty());

    empty += ' ';
    empty += '\t';
    empty += '\n';
    empty += '\f';
    ck_assert(!empty.empty());

    gu::trim (empty);

    ck_assert_msg(empty.empty(), "string contents: '%s', expected empty",
                  empty.c_str());
}
END_TEST

Suite* gu_string_utils_suite(void)
{
    Suite* s = suite_create("String Utils");
    TCase* tc;

    tc = tcase_create("strsplit");
    tcase_add_test(tc, test_strsplit);
    suite_add_tcase(s, tc);

    tc = tcase_create("tokenize");
    tcase_add_test(tc, test_tokenize);
    suite_add_tcase(s, tc);

    tc = tcase_create("trim");
    tcase_add_test(tc, test_trim);
    suite_add_tcase(s, tc);

    return s;
}
