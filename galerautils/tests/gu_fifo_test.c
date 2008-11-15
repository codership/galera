// Copyright (C) 2007 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gu_fifo_test.h"
#include "../src/galerautils.h"

#define FIFO_LENGTH 10000UL

START_TEST (gu_fifo_test)
{
    gu_fifo_t* fifo;
    long i;
    long* item;
    size_t alloc, used;

    fifo = gu_fifo_create (0, 1);
    fail_if (fifo != NULL);

    fifo = gu_fifo_create (1, 0);
    fail_if (fifo != NULL);

    fifo = gu_fifo_create (1, 1);
    fail_if (fifo == NULL);
    fail_if (fifo->length == 1); // must be longer
    gu_fifo_destroy (fifo);
    mark_point();

    fifo = gu_fifo_create (FIFO_LENGTH, sizeof(i));
    fail_if (fifo == NULL);
    fail_if ((fifo->length) < FIFO_LENGTH,
             "Total fifo length %lu, expected > %lu",
             fifo->length, FIFO_LENGTH);
    fail_if (fifo->used != 0, "fifo->used is %lu for an empty FIFO",
             fifo->used);

    alloc = fifo->alloc; // remember how much allocated for empty FIFO.

    // fill FIFO
    for (i = 0; i < fifo->length; i++) {
        item = gu_fifo_get_tail (fifo);
        fail_if (item == NULL, "could not get item %ld", i);
        *item = i;
        gu_fifo_push_tail (fifo);
    }

    used = i;
    fail_if (fifo->length != used, "used is %zu, expected %zu", 
             used, fifo->length);
    fail_if (fifo->used != used, "fifo->used is %zu, expected %zu", 
             fifo->used, used);
    fail_if (fifo->used != gu_fifo_length (fifo),
             "gu_fifo_length() shows %ld, should  %lu",
             gu_fifo_length(fifo), fifo->used);

    // test pop
    for (i = 0; i < used; i++) {
        item = gu_fifo_get_head (fifo);
        fail_if (item == NULL, "could not get item %ld", i);
        fail_if (*item != i, "got %ld, expected %ld", *item, i);
        gu_fifo_pop_head (fifo);
    }

    fail_if (fifo->used != 0, "fifo->used for empty queue is %ld", fifo->used);
    fail_if (fifo->alloc != alloc,
             "After emtying fifo, alloc counter is not "
             "the same as in the beginning: was %lu, got %lu",
             alloc, fifo->alloc);

    gu_fifo_close (fifo);
    mark_point ();
    gu_fifo_destroy (fifo);
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

