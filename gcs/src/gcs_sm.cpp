/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file GCS Send Monitor. To ensure fair (FIFO) access to gcs_core_send()
 */

#include "gcs_sm.hpp"

#include <string.h>

static void
sm_init_stats (gcs_sm_stats_t* stats)
{
    stats->sample_start   = gu_time_monotonic();
    stats->pause_start    = 0;
    stats->paused_for     = 0;
    stats->send_q_samples = 0;
    stats->send_q_len     = 0;
}

gcs_sm_t*
gcs_sm_create (long len, long n)
{
    if ((len < 2 /* 2 is minimum */) || (len & (len - 1))) {
        gu_error ("Monitor length parameter is not a power of 2: %ld", len);
        return NULL;
    }

    if (n < 1) {
        gu_error ("Invalid monitor concurrency parameter: %ld", n);
        return NULL;
    }

    size_t sm_size = sizeof(gcs_sm_t) +
        len * sizeof(((gcs_sm_t*)(0))->wait_q[0]);

    gcs_sm_t* sm = static_cast<gcs_sm_t*>(gu_malloc(sm_size));

    if (sm) {
        sm_init_stats (&sm->stats);
        gu_mutex_init (&sm->lock, NULL);
#ifdef GCS_SM_GRAB_RELEASE
        gu_cond_init  (&sm->cond, NULL);
        sm->cond_wait   = 0;
#endif /* GCS_SM_GRAB_RELEASE */
        sm->wait_q_len  = len;
        sm->wait_q_mask = sm->wait_q_len - 1;
        sm->wait_q_head = 1;
        sm->wait_q_tail = 0;
        sm->users       = 0;
        sm->entered     = 0;
        sm->ret         = 0;
#ifdef GCS_SM_CONCURRENCY
        sm->cc          = n; // concurrency param.
#endif /* GCS_SM_CONCURRENCY */
        sm->pause       = false;
        memset (sm->wait_q, 0, sm->wait_q_len * sizeof(sm->wait_q[0]));
    }

    return sm;
}

long
gcs_sm_close (gcs_sm_t* sm)
{
    gu_info ("Closing send monitor...");

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->ret = -EBADFD;

    if (sm->pause) _gcs_sm_continue_common (sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    // in case the queue is full
    while (sm->users >= (long)sm->wait_q_len) {
        gu_mutex_unlock (&sm->lock);
        usleep(1000);
        gu_mutex_lock (&sm->lock);
    }

    while (sm->users > 0) { // wait for cleared queue
        sm->users++;
        GCS_SM_INCREMENT(sm->wait_q_tail);
        _gcs_sm_enqueue_common (sm, &cond);
        sm->users--;
        GCS_SM_INCREMENT(sm->wait_q_head);
    }

    gu_cond_destroy (&cond);

    gu_mutex_unlock (&sm->lock);

    gu_info ("Closed send monitor.");

    return 0;
}

long
gcs_sm_open (gcs_sm_t* sm)
{
    long ret = -1;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    if (-EBADFD == sm->ret)  /* closed */
    {
        sm->ret = 0;
    }
    ret = sm->ret;

    gu_mutex_unlock (&sm->lock);

    if (ret) { gu_error ("Can't open send monitor: wrong state %d", ret); }

    return ret;
}

void
gcs_sm_destroy (gcs_sm_t* sm)
{
    gu_mutex_destroy(&sm->lock);
    gu_free (sm);
}

void
gcs_sm_stats (gcs_sm_t* sm, long* q_len, double* q_len_avg, double* paused_for)
{
    gcs_sm_stats_t tmp;
    long long      now;
    bool           paused;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    *q_len = sm->users;
    tmp    = sm->stats;
    now    = gu_time_monotonic();
    paused = sm->pause;

    sm->stats.sample_start = now;
    sm->stats.pause_start  = now; // if we are in paused state this is true
                                  // and if not - gcs_sm_pause() will correct it
    sm->stats.paused_for     = 0;
    sm->stats.send_q_samples = 0;
    sm->stats.send_q_len     = 0;

    gu_mutex_unlock (&sm->lock);

    if (paused) { // taking sample in a middle of a pause
        tmp.paused_for += now - tmp.pause_start;
    }

    if (tmp.paused_for >= 0) {
        *paused_for = ((double)tmp.paused_for) / (now - tmp.sample_start);
    }
    else {
        *paused_for = -1.0;
    }

    if (tmp.send_q_len >= 0 && tmp.send_q_samples >= 0){
        if (tmp.send_q_samples > 0) {
            *q_len_avg = ((double)tmp.send_q_len) / tmp.send_q_samples;
        }
        else {
            *q_len_avg = 0.0;
        }
    }
    else {
        *q_len_avg = -1.0;
    }
}
