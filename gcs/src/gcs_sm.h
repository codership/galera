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

typedef struct gcs_sm
{
    gu_mutex_t    lock;
    unsigned long wait_q_size;
    unsigned long wait_q_mask;
    unsigned long wait_q_head;
    long          wait_q_len;
    long          ret;
    bool          pause;
    bool          entered;
    gu_cond_t*    wait_q[];
}
gcs_sm_t;

extern gcs_sm_t*
gcs_sm_create (unsigned long len);

extern long
gcs_sm_close (gcs_sm_t* sm);

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
 */
static inline long
gcs_sm_enter (gcs_sm_t* sm, gu_cond_t* cond)
{
    long ret;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->wait_q_len++;

    if ((sm->wait_q_len > 0 || sm->pause) && 0 == sm->ret) {
        _gcs_sm_enqueue_unsafe (sm, cond);
    }

    ret = sm->ret;
    if (gu_likely(0 == ret)) {
        sm->entered = true;
    }
    else {
        _gcs_sm_leave_unsafe(sm);
    }

    gu_mutex_unlock (&sm->lock);

    return ret;
}

static inline void
gcs_sm_leave (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    _gcs_sm_leave_unsafe(sm);

    sm->entered = false;

    gu_mutex_unlock (&sm->lock);
}

static inline void
gcs_sm_pause (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->pause = true;

    gu_mutex_unlock (&sm->lock);    
}

static inline void
gcs_sm_continue (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    if (gu_likely(sm->pause)) {

        sm->pause = false;

        if (!sm->entered && sm->wait_q_len >= 0) {
            // there's no one to leave the monitor and signal the rest
            assert (sm->wait_q[sm->wait_q_head] != NULL);
            gu_cond_signal (sm->wait_q[sm->wait_q_head]);
        }
    }
    else {
        gu_debug ("Trying to continue unpaused monitor");
        assert(0);
    }

    gu_mutex_unlock (&sm->lock);    
}

#endif /* _gcs_sm_h_ */
