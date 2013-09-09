/* Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "../src/gu_string.hpp"

#include "gu_string_test.hpp"

START_TEST (ctor_test)
{
    gu::String<8> str1;                                // default
    fail_if (str1.size() != 0);
    fail_if (strlen(str1.c_str()) != 0);

    const char* const test_string1("test");

    gu::String<8> str2(test_string1);                  // from char*
    fail_if (str2.size() != strlen(test_string1));
    fail_if (strcmp(str2.c_str(), test_string1));

    gu::String<2> str3(str2);                          // copy ctor
    fail_if (str3.size() != str2.size());
    fail_if (strcmp(str2.c_str(), str3.c_str()));

    std::string const std_string(str3.c_str());

    gu::String<4> str4(std_string);                    // from std::string
    fail_if (str4.size() != strlen(test_string1));
    fail_if (strcmp(str4.c_str(), test_string1));

    gu::String<5> str5(test_string1, 2);
    fail_if (str5.size() != 2);
    fail_if (strncmp(str5.c_str(), test_string1, 2));
}
END_TEST

START_TEST (func_test)
{
    gu::String<16> str;
    fail_if (str.size() != 0);
    fail_if (strlen(str.c_str()) != 0);

    const char* const buf_ptr(str.c_str());

    str = "one";
    str << std::string("two") << gu::String<8>("three");

    fail_if (strcmp(str.c_str(), "onetwothree"));
    fail_if (str.c_str() != buf_ptr);

    str += "blast!"; // this should spill to heap

    fail_if (strcmp(str.c_str(), "onetwothreeblast!"),
             "expected 'onetwothreeblast!' got '%s'", str.c_str());
    fail_if (str.c_str() == buf_ptr);

    str = gu::String<2>("back to stack");

    fail_if (str != "back to stack");
    fail_if (str != gu::String<>("back to stack"));
    fail_if (str != std::string("back to stack"));
    fail_if (str.c_str() != buf_ptr);

    typedef void* pointer;

    // conversions
    fail_if ((gu::String<>() << true)   != "true");
    fail_if ((gu::String<>() << 0.0123) != "0.012300");
    if (sizeof(pointer) == 4)
        fail_if ((gu::String<>() << pointer(0xdeadbeef))!="0xdeadbeef");
    else
        fail_if ((gu::String<>() << pointer(0xdeadbeef))!="0x00000000deadbeef");

    fail_if ((gu::String<>() << 1234567890) != "1234567890");
    fail_if ((gu::String<>() << 12345U) != "12345");
    fail_if ((gu::String<>() << 'a') != "a");

    fail_if ((gu::String<>() << 0xdeadbeef) != "3735928559");
    fail_if ((gu::String<>() << gu::Fmt("%010x") << 0xdeadbeef) !="00deadbeef");
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
