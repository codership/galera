// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#define DEBUG_MALLOC // turn on the debugging code
#define TEST_SIZE 1024

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "gu_mem_test.h"
#include "../src/galerautils.h"

START_TEST (gu_mem_test)
{
    void* ptr1;
    void* ptr2;
    int   res;
    int   i;

    ptr1 = gu_malloc (0);
    ck_assert_msg(NULL == ptr1, "Zero memory allocated, non-NULL pointer returned");

    mark_point();
    ptr1 = gu_malloc (TEST_SIZE);
    ck_assert_msg(NULL != ptr1, "NULL pointer returned for allocation"
                  " errno: %s", strerror (errno));

    mark_point();
    ptr2 = memset (ptr1, 0xab, TEST_SIZE);
    ck_assert_msg(ptr2 == ptr1, "Memset changed pointer");

    ptr2 = NULL;
    mark_point();
    ptr2 = gu_realloc (ptr2, TEST_SIZE);
    ck_assert_msg(NULL != ptr2, "NULL pointer returned for reallocation"
                  " errno: %s", strerror (errno));

    memcpy (ptr2, ptr1, TEST_SIZE);

    mark_point();
    ptr1 = gu_realloc (ptr1, TEST_SIZE + TEST_SIZE);
    res = memcmp (ptr1, ptr2, TEST_SIZE);
    ck_assert_msg(res == 0, "Realloc changed the contents of the memory");

    mark_point();
    ptr1 = gu_realloc (ptr1, 0);
    ck_assert_msg(res == 0, "Realloc to 0 didn't return NULL");

    mark_point();
    ptr1 = gu_calloc (1, TEST_SIZE);
    ck_assert_msg(NULL != ptr1, "NULL pointer returned for allocation"
                  " errno: %s", strerror (errno));

    for (i = 0; i < TEST_SIZE; i++) {
        res = ((char*)ptr1)[i];
        if (res != 0) break;
    }
    ck_assert_msg(res == 0, "Calloc didn't clear up the memory");

    mark_point();
    gu_free (ptr1);
    mark_point();
    gu_free (ptr2);
}
END_TEST

Suite *gu_mem_suite(void)
{
  Suite *s = suite_create("Galera memory utils");
  TCase *tc_mem = tcase_create("gu_mem");

  suite_add_tcase (s, tc_mem);
  tcase_add_test(tc_mem, gu_mem_test);
  return s;
}

