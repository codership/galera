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

typedef struct gcs_sm_user
{
    gu_cond_t* cond;
    bool       wait;
}
gcs_sm_user_t;

typedef struct gcs_sm
{
    gu_mutex_t    lock;
    unsigned long wait_q_size;
    unsigned long wait_q_mask;
    unsigned long wait_q_head;
    long          wait_q_len;
    long          entered;
    long          ret;
    long          c;
    bool          pause;
    gcs_sm_user_t wait_q[];
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

#if 0
static inline void
_gcs_sm_leave_unsafe (gcs_sm_t* sm)
{
    do {
        sm->wait_q_len--;

        register bool next = (sm->wait_q_len >= 0);
        sm->wait_q_head = (sm->wait_q_head + next) & sm->wait_q_mask;

        if (next && !sm->pause) {
            if (gu_unlikely(false == sm->wait_q[sm->wait_q_head].wait)) {
                assert (NULL == sm->wait_q[sm->wait_q_head].cond);
                continue; /* interrupted */
            }
            assert (NULL != sm->wait_q[sm->wait_q_head].cond);
            gu_cond_signal (sm->wait_q[sm->wait_q_head].cond);
        }
        break;
    } while (true);
}
#else
static inline void
_gcs_sm_leave_unsafe (gcs_sm_t* sm)
{
    sm->wait_q_len--;

    while (sm->wait_q_len > sm->c) {

        sm->wait_q_head = (sm->wait_q_head + 1) & sm->wait_q_mask;

        if (gu_likely(sm->wait_q[sm->wait_q_head].wait)) {
            assert (NULL != sm->wait_q[sm->wait_q_head].cond);
            if (!sm->pause) {
                gu_cond_signal (sm->wait_q[sm->wait_q_head].cond);
            }
            return;
        }
        assert (NULL == sm->wait_q[sm->wait_q_head].cond);
        sm->wait_q_len--;
    }
}
#endif

#define GCS_SM_TAIL(sm) ((sm->wait_q_head + sm->wait_q_len) & sm->wait_q_mask)

static inline bool
_gcs_sm_enqueue_unsafe (gcs_sm_t* sm, gu_cond_t* cond)
{
    unsigned long tail = GCS_SM_TAIL(sm);

    sm->wait_q[tail].cond = cond;
    sm->wait_q[tail].wait = true;
    gu_cond_wait (cond, &sm->lock);
    assert(tail == sm->wait_q_head || false == sm->wait_q[tail].wait);
    assert(sm->wait_q[tail].cond == cond || false == sm->wait_q[tail].wait);
    sm->wait_q[tail].cond = NULL;
    register bool ret = sm->wait_q[tail].wait;
    sm->wait_q[tail].wait = false;
    return ret;
}

/*!
 * Synchronize with entry order to the monitor. Must be always followed by
 * gcs_sm_enter(sm, cond, true)
 *
 * @retval -EAGAIN - out of space
 * @retval -EBADFD - monitor closed
 * @retval >= 0 queue handle
 */
static inline long
gcs_sm_schedule (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    long ret = sm->ret;

    if (gu_likely((sm->wait_q_len < (long)sm->wait_q_size) &&
                  (0 == ret))) {

        sm->wait_q_len++;

        if ((sm->wait_q_len > 0 || sm->pause)) ret = GCS_SM_TAIL(sm) + 1;

        return ret; // success
    }
    else if (0 == ret) {
        ret = -EAGAIN;
    }

    assert(ret < 0);

    gu_mutex_unlock (&sm->lock);

    return ret;
}

/*!
 * Enter send monitor critical section
 *
 * @param sm   send monitor object
 * @param cond condition to signal to wake up thread in case of wait
 *
 * @retval -EAGAIN - out of space
 * @retval -EBADFD - monitor closed
 * @retval -EINTR  - was interrupted by another thread
 * @retval 0 - successfully entered
 */
static inline long
gcs_sm_enter (gcs_sm_t* sm, gu_cond_t* cond, bool scheduled)
{
    long ret = 0; /* if scheduled and no queue */

    if (gu_likely (scheduled || (ret = gcs_sm_schedule(sm)) >= 0)) {

        if (sm->wait_q_len > 0 || sm->pause) {
            if (gu_likely(_gcs_sm_enqueue_unsafe (sm, cond))) {
                ret = sm->ret;
            }
            else {
                ret = -EINTR;
            }
        }

        assert (ret <= 0);

        if (gu_likely(0 == ret)) {
            sm->entered++;
        }
        else {
            if (gu_likely(-EINTR == ret)) {
                /* was interrupted, will be handled by the leaving guy */
                if (sm->entered == 0 && sm->pause == false &&
                    sm->wait_q_len == sm->c + 1)
                {
                    _gcs_sm_leave_unsafe(sm);
                }
            }
            else {
                /* monitor is closed, wake up others */
                _gcs_sm_leave_unsafe(sm);
            }
        }

        gu_mutex_unlock (&sm->lock);
    }

    return ret;
}

static inline void
gcs_sm_leave (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->entered--;
    assert(sm->entered >= 0);

    _gcs_sm_leave_unsafe(sm);

    gu_mutex_unlock (&sm->lock);
}

static inline void
gcs_sm_pause (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->pause = (sm->ret == 0); /* don't pause closed monitor */

    gu_mutex_unlock (&sm->lock);
}

static inline void
_gcs_sm_continue_unsafe (gcs_sm_t* sm)
{
    sm->pause = false;

    if (0 == sm->entered) {
        // there's no one to leave the monitor and signal the rest
        while (sm->wait_q_len > sm->c) {
            if (gu_likely(sm->wait_q[sm->wait_q_head].wait)) {
                assert (sm->wait_q[sm->wait_q_head].cond != NULL);
                gu_cond_signal (sm->wait_q[sm->wait_q_head].cond);
                return;
            }
            /* skip interrupted */
            gu_debug ("Skipping interrupted thread");
            sm->wait_q_len--;
            sm->wait_q_head = (sm->wait_q_head + 1) & sm->wait_q_mask;
        }
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

/*!
 * Interrupts waiter identified by handle (returned by gcs_sm_schedule())
 *
 * @retval 0 - success
 * @retval -ESRCH - waiter is not in the queue. For practical purposes
 *                  it is impossible to discern already interrupted waiter and
 *                  the waiter that has entered the monitor
 */
static inline long
gcs_sm_interrupt (gcs_sm_t* sm, long handle)
{
    assert (handle > 0);
    long ret;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    handle--;

    if (gu_likely(sm->wait_q[handle].wait)) {
        assert (sm->wait_q[handle].cond != NULL);
        sm->wait_q[handle].wait = false;
        gu_cond_signal (sm->wait_q[handle].cond);
        sm->wait_q[handle].cond = NULL;
        ret = 0;
    }
    else {
        ret = -ESRCH;
    }

    gu_mutex_unlock (&sm->lock);

    return ret;
}

#endif /* _gcs_sm_h_ */
