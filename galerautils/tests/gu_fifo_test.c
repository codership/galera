// Copyright (C) 2007-2020 Codership Oy <info@codership.com>

// $Id$

#include <check.h>
#include "gu_fifo_test.h"
#include "../src/galerautils.h"

#define FIFO_LENGTH 10000L

START_TEST (gu_fifo_test)
{
    gu_fifo_t* fifo;
    long i;
    size_t* item;
    long used;

    fifo = gu_fifo_create (0, 1);
    ck_assert(fifo == NULL);

    fifo = gu_fifo_create (1, 0);
    ck_assert(fifo == NULL);

    fifo = gu_fifo_create (1, 1);
    ck_assert(fifo != NULL);
    gu_fifo_close   (fifo);
    mark_point();
    gu_fifo_destroy (fifo);
    mark_point();

    fifo = gu_fifo_create (FIFO_LENGTH, sizeof(i));
    ck_assert(fifo != NULL);
    ck_assert_msg(gu_fifo_length(fifo) == 0,
                  "fifo->used is %lu for an empty FIFO",
                  gu_fifo_length(fifo));

    mark_point();
    gu_fifo_clear(fifo); // clear empty fifo
    ck_assert_msg(gu_fifo_length(fifo) == 0,
                  "fifo->used is %lu for a cleared FIFO",
                  gu_fifo_length(fifo));

    // fill FIFO
    for (i = 0; i < FIFO_LENGTH; i++) {
        item = gu_fifo_get_tail (fifo);
        ck_assert_msg(item != NULL, "could not get item %ld", i);
        *item = i;
        gu_fifo_push_tail (fifo);
    }

    used = i;
    ck_assert_msg(gu_fifo_length(fifo) == used, "used is %zu, expected %zu",
                  used, gu_fifo_length(fifo));

    mark_point();
    gu_fifo_clear(fifo); // clear filled fifo
    ck_assert_msg(gu_fifo_length(fifo) == 0,
                  "fifo->used is %lu for a cleared FIFO",
                  gu_fifo_length(fifo));

    // fill FIFO again
    for (i = 0; i < FIFO_LENGTH; i++) {
        item = gu_fifo_get_tail (fifo);
        ck_assert_msg(item != NULL, "could not get item %ld", i);
        *item = i;
        gu_fifo_push_tail (fifo);
    }

    used = i;
    ck_assert_msg(gu_fifo_length(fifo) == used, "used is %zu, expected %zu",
                  used, gu_fifo_length(fifo));

    // test pop
    for (i = 0; i < used; i++) {
        int err;
        item = gu_fifo_get_head (fifo, &err);
        ck_assert_msg(item != NULL, "could not get item %ld", i);
        ck_assert_msg(*item == (ulong)i, "got %ld, expected %ld", *item, i);
        gu_fifo_pop_head (fifo);
    }

    ck_assert_msg(gu_fifo_length(fifo) == 0,
                  "gu_fifo_length() for empty queue is %ld",
                  gu_fifo_length(fifo));

    gu_fifo_close (fifo);

    int err;
    item = gu_fifo_get_head (fifo, &err);
    ck_assert(item == NULL);
    ck_assert(err  == -ENODATA);

    gu_fifo_destroy (fifo);
}
END_TEST

static gu_mutex_t
sync_mtx = GU_MUTEX_INITIALIZER;

static gu_cond_t
sync_cond = GU_COND_INITIALIZER;

#define ITEM 12345

static void*
cancel_thread (void* arg)
{
    gu_fifo_t* q = arg;

    /* sync with parent */
    gu_mutex_lock (&sync_mtx);
    gu_cond_signal (&sync_cond);
    gu_mutex_unlock (&sync_mtx);

    size_t* item;
    int     err;

    /* try to get from non-empty queue */
    item = gu_fifo_get_head (q, &err);
    ck_assert_msg(NULL == item, "Got item %p: %zu", item, item ? *item : 0);
    ck_assert(-ECANCELED == err);

    /* signal end of the first gu_fifo_get_head() */
    gu_mutex_lock (&sync_mtx);
    gu_cond_signal (&sync_cond);
    /* wait until gets are resumed */
    gu_cond_wait (&sync_cond, &sync_mtx);

    item = gu_fifo_get_head (q, &err);
    ck_assert(NULL != item);
    ck_assert(ITEM == *item);
    gu_fifo_pop_head (q);

    /* signal end of the 2nd gu_fifo_get_head() */
    gu_cond_signal (&sync_cond);
    gu_mutex_unlock (&sync_mtx);

    /* try to get from empty queue (should block) */
    item = gu_fifo_get_head (q, &err);
    ck_assert(NULL == item);
    ck_assert(-ECANCELED == err);

    /* signal end of the 3rd gu_fifo_get_head() */
    gu_mutex_lock (&sync_mtx);
    gu_cond_signal (&sync_cond);
    /* wait until fifo is closed */
    gu_cond_wait (&sync_cond, &sync_mtx);

    item = gu_fifo_get_head (q, &err);
    ck_assert(NULL == item);
    ck_assert(-ECANCELED == err);

    /* signal end of the 4th gu_fifo_get_head() */
    gu_cond_signal (&sync_cond);
    /* wait until fifo is resumed */
    gu_cond_wait (&sync_cond, &sync_mtx);
    gu_mutex_unlock (&sync_mtx);

    item = gu_fifo_get_head (q, &err);
    ck_assert(NULL == item);
    ck_assert(-ENODATA == err);

    return NULL;
}

START_TEST(gu_fifo_cancel_test)
{
    gu_fifo_t* q = gu_fifo_create (FIFO_LENGTH, sizeof(size_t));

    size_t* item = gu_fifo_get_tail (q);
    ck_assert(item != NULL);
    *item = ITEM;
    gu_fifo_push_tail (q);

    gu_mutex_lock (&sync_mtx);

    gu_thread_t thread;
    gu_thread_create (&thread, NULL, cancel_thread, q);

    /* sync with child thread */
    gu_fifo_lock (q);
    gu_cond_wait (&sync_cond, &sync_mtx);

    int err;
    err = gu_fifo_cancel_gets (q);
    ck_assert(0 == err);
    err = gu_fifo_cancel_gets (q);
    ck_assert(-EBADFD == err);

    /* allow the first gu_fifo_get_head() */
    gu_fifo_release (q);
    mark_point();

    /* wait for the first gu_fifo_get_head() to complete */
    gu_cond_wait (&sync_cond, &sync_mtx);
    mark_point();

    err = gu_fifo_resume_gets (q);
    ck_assert(0 == err);
    err = gu_fifo_resume_gets (q);
    ck_assert(-EBADFD == err);

    /* signal that now gets are resumed */
    gu_cond_signal (&sync_cond);
    /* wait for the 2nd gu_fifo_get_head() to complete */
    gu_cond_wait (&sync_cond, &sync_mtx);

    /* wait a bit to make sure 3rd gu_fifo_get_head() is blocked
     * (even if it is not - still should work)*/
    usleep (100000 /* 0.1s */);
    gu_fifo_lock(q);
    err = gu_fifo_cancel_gets (q);
    gu_fifo_release(q);
    ck_assert(0 == err);

    /* wait for the 3rd gu_fifo_get_head() to complete */
    gu_cond_wait (&sync_cond, &sync_mtx);

    gu_fifo_close (q); // closes for puts, but the q still must be canceled

    gu_cond_signal (&sync_cond);
    /* wait for the 4th gu_fifo_get_head() to complete */
    gu_cond_wait (&sync_cond, &sync_mtx);

    gu_fifo_resume_gets (q); // resumes gets

    gu_cond_signal (&sync_cond);
    gu_mutex_unlock (&sync_mtx);

    mark_point();

    gu_thread_join(thread, NULL);

    gu_fifo_destroy(q);
}
END_TEST

Suite *gu_fifo_suite(void)
{
    Suite *s  = suite_create("Galera FIFO functions");
    TCase *tc = tcase_create("gu_fifo");

    suite_add_tcase (s, tc);
    tcase_add_test  (tc, gu_fifo_test);
    tcase_add_test  (tc, gu_fifo_cancel_test);
    tcase_set_timeout(tc, 60);

    return s;
}

