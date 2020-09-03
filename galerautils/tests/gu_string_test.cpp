/* Copyright (C) 2013-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../src/gu_string.hpp"

#include "gu_string_test.hpp"

START_TEST (ctor_test)
{
    gu::String<8> str1;                                // default
    ck_assert(str1.size() == 0);
    ck_assert(strlen(str1.c_str()) == 0);

    const char* const test_string1("test");

    gu::String<8> str2(test_string1);                  // from char*
    ck_assert(str2.size() == strlen(test_string1));
    ck_assert(::strcmp(str2.c_str(), test_string1) == 0);

    gu::String<2> str3(str2);                          // copy ctor
    ck_assert(str3.size() == str2.size());
    ck_assert(::strcmp(str2.c_str(), str3.c_str()) == 0);

    std::string const std_string(str3.c_str());

    gu::String<4> str4(std_string);                    // from std::string
    ck_assert(str4.size() == strlen(test_string1));
    ck_assert(::strcmp(str4.c_str(), test_string1) == 0);

    gu::String<5> str5(test_string1, 2);
    ck_assert(str5.size() == 2);
    ck_assert(::strncmp(str5.c_str(), test_string1, 2) == 0);
}
END_TEST

START_TEST (func_test)
{
    gu::String<16> str;
    ck_assert(str.size() == 0);
    ck_assert(strlen(str.c_str()) == 0);

    const char* const buf_ptr(str.c_str());

    str = "one";
    str << std::string("two") << gu::String<8>("three");

    ck_assert(::strcmp(str.c_str(), "onetwothree") == 0);
    ck_assert(str.c_str() == buf_ptr);

    str += "blast!"; // this should spill to heap

    ck_assert_msg(::strcmp(str.c_str(), "onetwothreeblast!") == 0,
                  "expected 'onetwothreeblast!' got '%s'", str.c_str());
    ck_assert(str.c_str() != buf_ptr);

    str = gu::String<2>("back to stack");

    ck_assert(str == "back to stack");
    ck_assert(str == gu::String<>("back to stack"));
    ck_assert(str == std::string("back to stack"));
    ck_assert(str.c_str() == buf_ptr);

    typedef void* pointer;

    // conversions
    ck_assert((gu::String<>() << true)   == "true");
    ck_assert((gu::String<>() << 0.0123) == "0.012300");
    if (sizeof(pointer) == 4)
        ck_assert((gu::String<>() << pointer(0xdeadbeef))=="0xdeadbeef");
    else
        ck_assert((gu::String<>() << pointer(0xdeadbeef))=="0x00000000deadbeef");

    ck_assert((gu::String<>() << 1234567890) == "1234567890");
    ck_assert((gu::String<>() << 12345U) == "12345");
    ck_assert((gu::String<>() << 'a') == "a");

    ck_assert((gu::String<>() << 0xdeadbeef) == "3735928559");
    ck_assert((gu::String<>() << gu::Fmt("%010x") << 0xdeadbeef) =="00deadbeef");
}
END_TEST

Suite*
gu_string_suite(void)
{
    Suite* s = suite_create ("gu::String");

    TCase* t = tcase_create ("ctor_test");
    tcase_add_test (t, ctor_test);
    suite_add_tcase (s, t);

    t = tcase_create ("func_test");
    tcase_add_test (t, func_test);
    suite_add_tcase (s, t);

    return s;
}
