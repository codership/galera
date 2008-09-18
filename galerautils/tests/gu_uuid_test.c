/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "gcs_uuid_test.h"
#include "../gcs_uuid.h"

START_TEST (gcs_uuid_test)
{
    /* fail("Not implemented"); */
}
END_TEST

Suite *gcs_uuid_suite(void)
{
  Suite *suite = suite_create("GCS UUID");
  TCase *tcase = tcase_create("gcs_uuid");

  suite_add_tcase (suite, tcase);
  tcase_add_test  (tcase, gcs_uuid_test);
  return suite;
}

