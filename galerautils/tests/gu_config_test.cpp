// Copyright (C) 2013-2014 Codership Oy <info@codership.com>

// $Id$

#include "../src/gu_config.hpp"

#include "gu_config_test.hpp"

static std::string const key("test_key");
static std::string const another_key("another_key");
static std::string const str_value("123");
static long long   const int_value( 123 );

START_TEST (gu_config_test)
{
    gu::Config  cnf;
    std::string svalue;
    long long   ivalue;

    fail_if(cnf.has(key));
    try { cnf.is_set(key); fail("gu::NotFound expected"); }
    catch(gu::NotFound&) {}

    cnf.add(key);

    fail_unless(cnf.has(key));
    fail_if(cnf.is_set(key));

#define SUFFIX_CHECK(_suf_,_shift_)             \
    svalue = str_value + _suf_;                 \
    cnf.set(key, svalue);                       \
    fail_unless(cnf.is_set(key));               \
    fail_unless(cnf.get(key) == svalue);        \
    ivalue = cnf.get<long long>(key);           \
    fail_if(ivalue != (int_value << _shift_));

    SUFFIX_CHECK('T', 40);

    // check overflow checks
    try { ivalue = cnf.get<char>(key);  fail("gu::Exception expected"); }
    catch (gu::Exception&) {}
    try { ivalue = cnf.get<short>(key); fail("gu::Exception expected"); }
    catch (gu::Exception&) {}
    try { ivalue = cnf.get<int>(key);   fail("gu::Exception expected"); }
    catch (gu::Exception&) {}

    SUFFIX_CHECK('G', 30);
    SUFFIX_CHECK('M', 20);
    SUFFIX_CHECK('K', 10);

//    try { cnf.add(key, str_value); fail("gu::Exception expected"); }
//    catch (gu::Exception& e) {}

    cnf.add(another_key, str_value);
    fail_unless(cnf.is_set(another_key));
    ivalue = cnf.get<long long>(another_key);
    fail_if(ivalue != int_value);
}
END_TEST

Suite *gu_config_suite(void)
{
  Suite *s  = suite_create("gu::Config");
  TCase *tc = tcase_create("gu_config_test");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_config_test);
  return s;
}

