/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file GCS Send Monitor. To ensure fair (FIFO) access to gcs_core_send()
 */

#ifndef _gcs_sm_h_
#define _gcs_sm_h_

#include <galerautils.h>
#include <errno.h>

typedef struct gcs_sm
{
    gu_mutex_t    lock;
    unsigned long wait_q_size;
    unsigned long wait_q_mask;
    unsigned long wait_q_head;
    long          wait_q_len;
    long          entered;
    long          ret;
    bool          pause;
    gu_cond_t*    wait_q[];
}
gcs_sm_t;

/*!
 * Creates send monitor
 *
 * @param len size of the monitor, should be a power of 2
 * @param n   concurrency parameter (how many users can enter at the same time)
 */
extern gcs_sm_t*
gcs_sm_create (long len, long n);

/*!
 * Closes monitor for entering and makes all waiters to exit with error.
 * (entered users are not affected). Blocks until everybody exits
 */
extern long
gcs_sm_close (gcs_sm_t* sm);

/*!
 * Deallocates resources associated with the monitor
 */
extern void
gcs_sm_destroy (gcs_sm_t* sm);

static inline void
_gcs_sm_leave_unsafe (gcs_sm_t* sm)
{
    register bool next = (sm->wait_q_len > 0);
    sm->wait_q_head = (sm->wait_q_head + next) & sm->wait_q_mask;
    sm->wait_q_len--;

    if (sm->wait_q_len >= 0 && !sm->pause) {
        assert (sm->wait_q[sm->wait_q_head] != NULL);
        gu_cond_signal (sm->wait_q[sm->wait_q_head]);
    }
}

static inline void
_gcs_sm_enqueue_unsafe (gcs_sm_t* sm, gu_cond_t* cond)
{
    unsigned long tail =
        (sm->wait_q_head + sm->wait_q_len) & sm->wait_q_mask;

    sm->wait_q[tail] = cond;
    gu_cond_wait (cond, &sm->lock);
    assert(tail == sm->wait_q_head);
    assert(sm->wait_q[tail] == cond);
    sm->wait_q[tail] = NULL;
}

/*!
 * Enter send monitor critical section
 *
 * @param sm   send monitor object
 * @param cond condition to signal to wake up thread in case of wait
 *
 * @return 0 - success, -EAGAIN - out of space, -EBADFD - monitor closed
 */
static inline long
gcs_sm_enter (gcs_sm_t* sm, gu_cond_t* cond, bool scheduled)
{
    long ret;

    if (gu_unlikely(!scheduled && gu_mutex_lock (&sm->lock))) abort();

    if (gu_likely (sm->wait_q_len < (long)sm->wait_q_size)) {

        sm->wait_q_len++;

        if ((sm->wait_q_len > 0 || sm->pause) && 0 == sm->ret) {
            _gcs_sm_enqueue_unsafe (sm, cond);
        }

        ret = sm->ret;

        if (gu_likely(0 == ret)) {
            sm->entered++;
        }
        else {
            _gcs_sm_leave_unsafe(sm);
        }
    }
    else {
        ret = -EAGAIN;
    }

    gu_mutex_unlock (&sm->lock);

    return ret;
}

/*!
 * Synchronize with entry order to the monitor. Must be always followed by
 * gcs_sm_enter(sm, cond, true)
 */
static inline void
gcs_sm_schedule (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();
}

static inline void
gcs_sm_leave (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    _gcs_sm_leave_unsafe(sm);

    sm->entered--;
    assert(sm->entered >= 0);

    gu_mutex_unlock (&sm->lock);
}

static inline void
gcs_sm_pause (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->pause = (sm->ret == 0); // don't pause closed monitor

    gu_mutex_unlock (&sm->lock);
}

static inline void
_gcs_sm_continue_unsafe (gcs_sm_t* sm)
{
    sm->pause = false;

    if (0 == sm->entered && sm->wait_q_len >= 0) {
        // there's no one to leave the monitor and signal the rest
        assert (sm->wait_q[sm->wait_q_head] != NULL);
        gu_cond_signal (sm->wait_q[sm->wait_q_head]);
    }
}

static inline void
gcs_sm_continue (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    if (gu_likely(sm->pause)) {
        _gcs_sm_continue_unsafe (sm);
    }
    else {
        gu_debug ("Trying to continue unpaused monitor");
        assert(0);
    }

    gu_mutex_unlock (&sm->lock);
}

#endif /* _gcs_sm_h_ */
