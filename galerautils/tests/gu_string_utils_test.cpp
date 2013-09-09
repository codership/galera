// Copyright (C) 2009-2010 Codership Oy <info@codership.com>

#include "gu_string_utils.hpp"
#include "gu_string_utils_test.hpp"

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

START_TEST(test_tokenize)
{
    vector<string> vec = gu::tokenize("", 'a', 'b', false);
    fail_unless(vec.size() == 0);

    vec = gu::tokenize("", 'a', 'b', true);
    fail_unless(vec.size() == 1);
    fail_unless(vec[0] == "");

    vec = gu::tokenize("a", 'a', 'b', false);
    fail_unless(vec.size() == 0);

    vec = gu::tokenize("a", 'a', 'b', true);
    fail_unless(vec.size() == 2);
    fail_unless(vec[0] == "");
    fail_unless(vec[1] == "");

    vec = gu::tokenize("foo bar baz");
    fail_unless(vec.size() == 3);
    fail_unless(vec[0] == "foo");
    fail_unless(vec[1] == "bar");
    fail_unless(vec[2] == "baz");

    vec = gu::tokenize("foo\\ bar baz");
    fail_unless(vec.size() == 2);
    fail_unless(vec[0] == "foo bar", "expected 'foo bar', found '%s'",
                vec[0].c_str());
    fail_unless(vec[1] == "baz");

    vec = gu::tokenize("foo\\;;bar;;baz;", ';', '\\', false);
    fail_unless(vec.size() == 3);
    fail_unless(vec[0] == "foo;");
    fail_unless(vec[1] == "bar");
    fail_unless(vec[2] == "baz");

    vec = gu::tokenize("foo\\;;bar;;baz;", ';', '\\', true);
    fail_unless(vec.size() == 5, "vetor length %zu, expected 5", vec.size());
    fail_unless(vec[0] == "foo;");
    fail_unless(vec[1] == "bar");
    fail_unless(vec[2] == "");
    fail_unless(vec[3] == "baz");
    fail_unless(vec[4] == "");
}
END_TEST

START_TEST(test_trim)
{
    string full1 = ".,wklerf joweji";
    string full2 = full1;

    gu::trim (full2);
    fail_if (full1 != full2);

    string part = " part ";

    gu::trim (part);
    fail_if (part.length() != 4);
    fail_if (0 != part.compare("part"));

    string empty;

    gu::trim (empty);
    fail_if (!empty.empty());

    empty += ' ';
    empty += '\t';
    empty += '\n';
    empty += '\f';
    fail_if (empty.empty());

    gu::trim (empty);

    fail_if (!empty.empty(), "string contents: '%s', expected empty",
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
