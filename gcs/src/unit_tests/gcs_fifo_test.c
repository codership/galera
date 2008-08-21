// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gcs_fifo_test.h"
#include "../gcs_fifo_lite.h"

#define FIFO_LENGTH 10

START_TEST (gcs_fifo_lite_test)
{
    gcs_fifo_lite_t* fifo;
    long ret;
    long i, j;
//    size_t used;

    fifo = gcs_fifo_lite_create (0, 1);
    fail_if (fifo != NULL);

    fifo = gcs_fifo_lite_create (1, 0);
    fail_if (fifo != NULL);

    fifo = gcs_fifo_lite_create (1, 1);
    fail_if (fifo == NULL);
    ret = gcs_fifo_lite_destroy (fifo);
    fail_if (ret != 0, "gcs_fifo_lite_destroy() returned %d", ret);

    fifo = gcs_fifo_lite_create (FIFO_LENGTH, sizeof(i));
    fail_if (fifo == NULL);
    fail_if (fifo->used != 0, "fifo->used is %z for an empty FIFO",
             fifo->used);

    // fill FIFO
    for (i = 1; i <= FIFO_LENGTH; i++) {
        ret = gcs_fifo_lite_put (fifo, &i);
        fail_if (ret != i, "gcs_fifo_lite_put() returned %ld, expected %ld",
                 ret, i);
    }
    fail_if (fifo->used != FIFO_LENGTH, "fifo->used is %zu, expected %zu", 
             fifo->used, FIFO_LENGTH);

    // test remove
    for (i = 1; i <= FIFO_LENGTH; i++) {
        ret = gcs_fifo_lite_remove (fifo);
        fail_if (ret != (FIFO_LENGTH - i),
                 "gcs_fifo_lite_remove() failed: %ld, i = %ld",
                 ret, i);
    }
    fail_if (fifo->used != 0, "fifo->used is %zu, expected %zu", 
             fifo->used, 0);

    // it should be possible to fill FIFO again
    for (i = 1; i <= FIFO_LENGTH; i++) {
        ret = gcs_fifo_lite_put (fifo, &i);
        fail_if (ret != i, "gcs_fifo_lite_put() returned %ld, expected %ld",
                 ret, i);
    }
    fail_if (fifo->used != FIFO_LENGTH, "fifo->used is %zu, expected %zu", 
             fifo->used, FIFO_LENGTH);

    // test get
    for (i = 1; i <= FIFO_LENGTH; i++) {
        ret = gcs_fifo_lite_get (fifo, &j);
        fail_if (ret != (FIFO_LENGTH - i), "gcs_fifo_lite_get() returned %ld,"
                 " expected %ld", ret, (FIFO_LENGTH - i));
        fail_if (j != i, "gcs_fifo_lite_get() data passed %ld, expected %ld",
                 j, i);
    }

    fail_if (fifo->used != 0, "fifo->used for empty queue is %ld", fifo->used);

    ret = gcs_fifo_lite_destroy (fifo);
    fail_if (ret != 0, "gcs_fifo_lite_destroy() failed: %d", ret);
}
END_TEST

Suite *gcs_fifo_suite(void)
{
  Suite *s  = suite_create("GCS FIFO functions");
  TCase *tc = tcase_create("gcs_fifo");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gcs_fifo_lite_test);
  return s;
}

