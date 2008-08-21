// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gu_fifo_test.h"
#include "../src/gu_fifo.h"

#define FIFO_LENGTH 10000

START_TEST (gu_fifo_test)
{
    gu_fifo_t* fifo;
    long ret;
    long i, j;
    size_t alloc, used;

    fifo = gu_fifo_create (0, 1);
    fail_if (fifo != NULL);

    fifo = gu_fifo_create (1, 0);
    fail_if (fifo != NULL);

    fifo = gu_fifo_create (1, 1);
    fail_if (fifo == NULL);
    fail_if (fifo->length == 1); // must be longer
    ret = gu_fifo_destroy (fifo);
    fail_if (ret != 0, "gu_fifo_destroy() returned %d", ret);

    fifo = gu_fifo_create (FIFO_LENGTH, sizeof(i));
    fail_if (fifo == NULL);
    fail_if ((fifo->length) < FIFO_LENGTH,"Total fifo length %z, expected > %d",
             fifo->length, FIFO_LENGTH);
    fail_if (fifo->used != 0, "fifo->used is %z for an empty FIFO",
             fifo->used);

    alloc = fifo->alloc; // remember how much allocated for empty FIFO.

    // fill FIFO (try to overflow)
    for (i = 1; i; i++) {
        ret = gu_fifo_push_lock (fifo, &i);
        if (-EAGAIN == ret) {
            break;
        }
        fail_if (ret != i, "gu_fifo_push_lock() returned %ld, expected %ld",
                 ret, i);
    }

    fail_if (-EAGAIN != ret);
    used = i - 1; // last push was unsuccessfull
    fail_if (fifo->used != used, "fifo->used is %zu, expected %zu", 
             fifo->used, used);

    // test iterator
    for (i = 1; i <= used; i++) {
        ret = gu_fifo_next_wait (fifo, &j);
        fail_if (ret != 0, "gu_fifo_next_wait() failed: %ld, i = %ld, %s",
                 ret, i, gu_fifo_print (fifo));
        fail_if (j != i, "gu_fifo_next_wait() data passed %ld, expected %ld",
                 j, i);
    }

    ret = gu_fifo_next_reset (fifo);
    fail_if (ret != 0, "gu_fifo_next_reset() failed: %ld", ret);
    ret = gu_fifo_next_wait (fifo, &j);
    fail_if (ret != 0, "gu_fifo_next_wait() failed: %ld", ret);
    fail_if (j != 1, "gu_fifo_next_wait() data passed %ld, expected %ld",j,1);

    fail_if (fifo->used != used, "fifo->used is %zu, expected %zu", 
             fifo->used, used);

    // test pop
    for (i = 1; i <= used; i++) {
        ret = gu_fifo_pop_wait (fifo, &j);
        fail_if (ret != (used - i), "gu_fifo_next_wait() returned %ld,"
                 " expected %ld", ret, (used - i));
        fail_if (j != i, "gu_fifo_next_wait() data passed %ld, expected %ld",
                 j, i);
    }

    fail_if (fifo->used != 0, "fifo->used for empty queue is %ld", fifo->used);
    fail_if (fifo->alloc != alloc, "After emtying fifo, alloc counter is not "
             "the same as in the beginning: was %lu, got %lu",
             alloc, fifo->alloc);

    ret = gu_fifo_destroy (fifo);
    fail_if (ret != 0, "gu_fifo_destroy() failed: %d", ret);
}
END_TEST

Suite *gu_fifo_suite(void)
{
  Suite *s  = suite_create("Galera FIFO functions");
  TCase *tc = tcase_create("gu_fifo");

  suite_add_tcase (s, tc);
  tcase_add_test  (tc, gu_fifo_test);
  return s;
}

