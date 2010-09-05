// Copyright (C) 2010 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gu_utils_test.h"
#include "../src/gu_utils.h"

#include <limits.h>
#include <stdint.h>

START_TEST (gu_strconv_test)
{
    long long   llret;
    const char* strret;

    strret = gu_str2ll ("-1a", &llret);
    fail_if (strret[0] != 'a');
    fail_if (-1 != llret);

    strret = gu_str2ll ("1K", &llret);
    fail_if (strret[0] != '\0');
    fail_if ((1 << 10) != llret);

    strret = gu_str2ll ("-1m", &llret);
    fail_if (strret[0] != '\0');
    fail_if (-(1 << 20) != llret);

    strret = gu_str2ll ("354G0", &llret);
    fail_if (strret[0] != '0');
    fail_if ((354LL << 30) != llret);

    strret = gu_str2ll ("0m", &llret);
    fail_if (strret[0] != '\0');
    fail_if (0 != llret);

    strret = gu_str2ll ("-999999999999999g", &llret);
    fail_if (strret[0] != '\0');
    fail_if (LLONG_MIN != llret);

    bool b;

    strret = gu_str2bool ("-1a", &b);
    fail_if (strret[0] != '-');
    fail_if (false != b);

    strret = gu_str2bool ("-1", &b);
    fail_if (strret[0] != '-');
    fail_if (false != b);

    strret = gu_str2bool ("1a", &b);
    fail_if (strret[0] != '1');
    fail_if (false != b);

    strret = gu_str2bool ("35", &b);
    fail_if (strret[0] != '3');
    fail_if (false != b);

    strret = gu_str2bool ("0k", &b);
    fail_if (strret[0] != '0');
    fail_if (false != b);

    strret = gu_str2bool ("1", &b);
    fail_if (strret[0] != '\0');
    fail_if (true != b);

    strret = gu_str2bool ("0", &b);
    fail_if (strret[0] != '\0');
    fail_if (false != b);

    strret = gu_str2bool ("Onn", &b);
    fail_if (strret[0] != 'O');
    fail_if (false != b);

    strret = gu_str2bool ("oFf", &b);
    fail_if (strret[0] != '\0');
    fail_if (false != b);

    strret = gu_str2bool ("offt", &b);
    fail_if (strret[0] != 'o');
    fail_if (false != b);

    strret = gu_str2bool ("On", &b);
    fail_if (strret[0] != '\0');
    fail_if (true != b);

    strret = gu_str2bool ("tru", &b);
    fail_if (strret[0] != 't');
    fail_if (false != b);

    strret = gu_str2bool ("trUE", &b);
    fail_if (strret[0] != '\0');
    fail_if (true != b);

    strret = gu_str2bool ("truEth", &b);
    fail_if (strret[0] != 't');
    fail_if (false != b);

    strret = gu_str2bool (" fALsE", &b);
    fail_if (strret[0] != ' ');
    fail_if (false != b);

    strret = gu_str2bool ("fALsE", &b);
    fail_if (strret[0] != '\0');
    fail_if (false != b);

    strret = gu_str2bool ("fALsEth", &b);
    fail_if (strret[0] != 'f');
    fail_if (false != b);

    void* ptr;
    strret = gu_str2ptr ("-01234abc", &ptr);
    fail_if (strret[0] != '\0');
    fail_if (-0x1234abcLL != (intptr_t)ptr, "Expected %lld, got %lld",
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

