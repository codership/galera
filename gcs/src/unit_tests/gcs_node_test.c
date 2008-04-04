/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "gcs_node_test.h"
#include "../gcs_node.h"

START_TEST (gcs_node_test)
{
    /* fail("Not implemented"); */
}
END_TEST

Suite *gcs_node_suite(void)
{
  Suite *suite = suite_create("GCS node context");
  TCase *tcase = tcase_create("gcs_node");

  suite_add_tcase (suite, tcase);
  tcase_add_test  (tcase, gcs_node_test);
  return suite;
}

