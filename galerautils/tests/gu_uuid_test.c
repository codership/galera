/*
 * Copyright (C) 2008-2020 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "../src/gu_log.h"
#include "../src/gu_uuid.h"
#include "gu_uuid_test.h"

START_TEST (gu_uuid_test)
{
    size_t    uuid_num = 10;
    gu_uuid_t uuid[uuid_num];
    size_t i;

    uuid[0] = GU_UUID_NIL;
    gu_uuid_generate (&uuid[0], NULL, 0);
    ck_assert(memcmp (&uuid[0], &GU_UUID_NIL, sizeof(gu_uuid_t)));
    ck_assert(gu_uuid_compare(&uuid[0], &GU_UUID_NIL));

    for (i = 1; i < uuid_num; i++) {
        uuid[i] = GU_UUID_NIL;
        gu_uuid_generate (&uuid[i], NULL, 0);
        ck_assert(gu_uuid_compare(&uuid[i], &GU_UUID_NIL));
        ck_assert(gu_uuid_compare(&uuid[i], &uuid[i - 1]));
        ck_assert(1  == gu_uuid_older (&uuid[i - 1], &uuid[i]));
        ck_assert(-1 == gu_uuid_older (&uuid[i], &uuid[i - 1]));
    }
}
END_TEST

Suite *gu_uuid_suite(void)
{
  Suite *suite = suite_create("Galera UUID utils");
  TCase *tcase = tcase_create("gu_uuid");

  suite_add_tcase (suite, tcase);
  tcase_add_test  (tcase, gu_uuid_test);
  return suite;
}

