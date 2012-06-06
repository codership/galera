// Copyright (C) 2012 Codership Oy <info@codership.com>

// $Id$

#include "gu_hash_test.h"

/* Tests partial hashing functions */
START_TEST (gu_hash_test)
{
}
END_TEST

Suite *gu_hash_suite(void)
{
  Suite *s  = suite_create("Galera hash");
  TCase *tc = tcase_create("gu_hash");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, gu_hash_test);

  return s;
}

