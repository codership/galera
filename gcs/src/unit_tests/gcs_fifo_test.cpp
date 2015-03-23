// Copyright (C) 2007-2015 Codership Oy <info@codership.com>

// $Id$

#include "../gcs_fifo_lite.hpp"

#include "gcs_fifo_test.hpp" // must be included last

#define FIFO_LENGTH 10

START_TEST (gcs_fifo_lite_test)
{
    gcs_fifo_lite_t* fifo;
    long ret;
    long i;
    long* item;

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

    gcs_fifo_lite_open (fifo);
    // fill FIFO
    for (i = 1; i <= FIFO_LENGTH; i++) {
        item = (long*)gcs_fifo_lite_get_tail (fifo);
        fail_if (NULL == item, "gcs_fifo_lite_get_tail() returned NULL");
        *item = i;
        gcs_fifo_lite_push_tail (fifo);
    }
    fail_if (fifo->used != FIFO_LENGTH, "fifo->used is %zu, expected %zu",
             fifo->used, FIFO_LENGTH);

    // test remove
    for (i = 1; i <= FIFO_LENGTH; i++) {
        ret = gcs_fifo_lite_remove (fifo);
        fail_if (0 == ret, "gcs_fifo_lite_remove() failed, i = %ld", i);
    }
    fail_if (fifo->used != 0, "fifo->used is %zu, expected %zu", 
             fifo->used, 0);

    // try remove on empty queue
    ret = gcs_fifo_lite_remove (fifo);
    fail_if (0 != ret, "gcs_fifo_lite_remove() from empty FIFO returned true");

    // it should be possible to fill FIFO again
    for (i = 1; i <= FIFO_LENGTH; i++) {
        item = (long*)gcs_fifo_lite_get_tail (fifo);
        fail_if (NULL == item, "gcs_fifo_lite_get_tail() returned NULL");
        *item = i;
        gcs_fifo_lite_push_tail (fifo);
    }
    fail_if (fifo->used != FIFO_LENGTH, "fifo->used is %zu, expected %zu", 
             fifo->used, FIFO_LENGTH);

    // test get
    for (i = 1; i <= FIFO_LENGTH; i++) {
        item = (long*)gcs_fifo_lite_get_head (fifo);
        fail_if (NULL == item, "gcs_fifo_lite_get_head() returned NULL");
        fail_if (*item != i, "gcs_fifo_lite_get_head() returned %ld, "
                 "expected %ld", *item, i);
        gcs_fifo_lite_release (fifo);
        item = (long*)gcs_fifo_lite_get_head (fifo);
        fail_if (NULL == item, "gcs_fifo_lite_get_head() returned NULL");
        fail_if (*item != i, "gcs_fifo_lite_get_head() returned %ld, "
                 "expected %ld", *item, i);
        gcs_fifo_lite_pop_head (fifo);
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

