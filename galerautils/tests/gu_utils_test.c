// Copyright (C) 2010-2020 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gu_utils_test.h"
#include "../src/gu_utils.h"

#include <limits.h>
#include <stdint.h>
#include <inttypes.h>

START_TEST (gu_strconv_test)
{
    long long   llret;
    const char* strret;

    strret = gu_str2ll ("-1a", &llret);
    ck_assert(strret[0] == 'a');
    ck_assert(-1 == llret);

    strret = gu_str2ll ("1K", &llret);
    ck_assert(strret[0] == '\0');
    ck_assert((1 << 10) == llret);

    strret = gu_str2ll ("-1m", &llret);
    ck_assert(strret[0] == '\0');
    ck_assert(-(1 << 20) == llret);

    strret = gu_str2ll ("354G0", &llret);
    ck_assert(strret[0] == '0');
    ck_assert((354LL << 30) == llret);

    strret = gu_str2ll ("0m", &llret);
    ck_assert(strret[0] == '\0');
    ck_assert(0 == llret);

    strret = gu_str2ll ("-999999999999999g", &llret);
    ck_assert(strret[0] == '\0');
    ck_assert(LLONG_MIN == llret);

    bool b;

    strret = gu_str2bool ("-1a", &b);
    ck_assert(strret[0] == '-');
    ck_assert(false == b);

    strret = gu_str2bool ("-1", &b);
    ck_assert(strret[0] == '-');
    ck_assert(false == b);

    strret = gu_str2bool ("1a", &b);
    ck_assert(strret[0] == '1');
    ck_assert(false == b);

    strret = gu_str2bool ("35", &b);
    ck_assert(strret[0] == '3');
    ck_assert(false == b);

    strret = gu_str2bool ("0k", &b);
    ck_assert(strret[0] == '0');
    ck_assert(false == b);

    strret = gu_str2bool ("1", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(true == b);

    strret = gu_str2bool ("0", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(false == b);

    strret = gu_str2bool ("Onn", &b);
    ck_assert(strret[0] == 'O');
    ck_assert(false == b);

    strret = gu_str2bool ("oFf", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(false == b);

    strret = gu_str2bool ("offt", &b);
    ck_assert(strret[0] == 'o');
    ck_assert(false == b);

    strret = gu_str2bool ("On", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(true == b);

    strret = gu_str2bool ("tru", &b);
    ck_assert(strret[0] == 't');
    ck_assert(false == b);

    strret = gu_str2bool ("trUE", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(true == b);

    strret = gu_str2bool ("truEth", &b);
    ck_assert(strret[0] == 't');
    ck_assert(false == b);

    strret = gu_str2bool (" fALsE", &b);
    ck_assert(strret[0] == ' ');
    ck_assert(false == b);

    strret = gu_str2bool ("fALsE", &b);
    ck_assert(strret[0] == '\0');
    ck_assert(false == b);

    strret = gu_str2bool ("fALsEth", &b);
    ck_assert(strret[0] == 'f');
    ck_assert(false == b);

    void* ptr;
    strret = gu_str2ptr ("-01234abc", &ptr);
    ck_assert(strret[0] == '\0');
    ck_assert_msg(-0x1234abcLL == (intptr_t)ptr,
                  "Expected %lld, got %" PRIdPTR,
                  -0x1234abcLL, (intptr_t)ptr);
}
END_TEST

Suite *gu_utils_suite(void)
{
  Suite *s  = suite_create("Galera misc utils functions");
  TCase *tc = tcase_create("gu_utils");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_strconv_test);
  return s;
}

