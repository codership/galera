// Copyright (C) 2013 Codership Oy <info@codership.com>

// $Id$

#include "gu_config_test.hpp"
#include "../src/gu_config.hpp"

START_TEST (gu_config_test)
{
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

