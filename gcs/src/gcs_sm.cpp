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
    stats->paused_ns      = 0;
    stats->paused_sample  = 0;
    stats->send_q_samples = 0;
    stats->send_q_len     = 0;
    stats->send_q_len_max = 0;
    stats->send_q_len_min = 0;
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
        gu_cond_init  (&sm->cond, NULL);
        sm->cond_wait   = 0;
        sm->wait_q_len  = len;
        sm->wait_q_mask = sm->wait_q_len - 1;
        sm->wait_q_head = 1;
        sm->wait_q_tail = 0;
        sm->users       = 0;
        sm->users_max   = 0;
        sm->users_min   = 0;
        sm->entered     = 0;
        sm->ret         = 0;
#ifdef GCS_SM_CONCURRENCY
        sm->cc          = n; // concurrency param.
#endif /* GCS_SM_CONCURRENCY */
        sm->pause       = false;
        sm->wait_time   = gu::datetime::Sec;

#ifdef GCS_SM_DEBUG
        memset (&sm->history, 0, sizeof(sm->history));
        sm->history_line = GCS_SM_HIST_LEN - 1; // point to the last line
#endif

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
        _gcs_sm_enqueue_common (sm, &cond, true, sm->wait_q_tail);
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
    gu_cond_destroy(&sm->cond);
    gu_free (sm);
}

void
gcs_sm_stats_get (gcs_sm_t*  sm,
                  int*       q_len,
                  int*       q_len_max,
                  int*       q_len_min,
                  double*    q_len_avg,
                  long long* paused_ns,
                  double*    paused_avg)
{
    gcs_sm_stats_t tmp;
    long long      now;
    bool           paused;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    *q_len_max = sm->users_max;
    *q_len_min = sm->users_min;
    *q_len = sm->users;
    tmp    = sm->stats;
    now    = gu_time_monotonic();
    paused = sm->pause;

    gu_mutex_unlock (&sm->lock);

    if (paused) { // taking sample in a middle of a pause
        tmp.paused_ns += now - tmp.pause_start;
    }
    *paused_ns = tmp.paused_ns;

    if (gu_likely(tmp.paused_ns >= 0)) {
        *paused_avg = ((double)(tmp.paused_ns - tmp.paused_sample)) /
                       (now - tmp.sample_start);
    }
    else {
        *paused_avg = -1.0;
    }

    if (gu_likely(tmp.send_q_len >= 0 && tmp.send_q_samples >= 0)){
        if (gu_likely(tmp.send_q_samples > 0)) {
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

void
gcs_sm_stats_flush(gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    long long const now = gu_time_monotonic();

    sm->stats.sample_start = now;

    sm->stats.paused_sample = sm->stats.paused_ns;

    if (sm->pause) // append elapsed pause time
    {
        sm->stats.paused_sample  += now - sm->stats.pause_start;
    }

    sm->stats.send_q_len     = 0;
    sm->stats.send_q_len_max = 0;
    sm->stats.send_q_len_min = 0;
    sm->stats.send_q_samples = 0;

    sm->users_max = sm->users;
    sm->users_min = sm->users;
    gu_mutex_unlock (&sm->lock);
}

#ifdef GCS_SM_DEBUG
void
_gcs_sm_dump_state_common(gcs_sm_t* sm, FILE* file)
{
    fprintf(
        file,
        "\nSend monitor state:"
        "\n==================="
        "\n\twait_q_len:  %lu"
        "\n\twait_q_head: %lu"
        "\n\twait_q_tail: %lu"
        "\n\tusers:       %ld"
        "\n\tentered:     %ld"
        "\n\tpaused:      %s"
        "\n\tstatus:      %ld\n",
        sm->wait_q_len,
        sm->wait_q_head,
        sm->wait_q_tail,
        sm->users,
        sm->entered,
        sm->pause ? "yes" : "no",
        sm->ret
        );

    fprintf(
        file,
        "\nSend monitor queue:"
        "\n===================\n"
        );

    unsigned long const pad(32);
    unsigned long const q_start(sm->wait_q_head >= pad ?
                                sm->wait_q_head - pad :
                                sm->wait_q_len + sm->wait_q_head - pad);
    unsigned long const q_end  ((sm->wait_q_tail + pad) % sm->wait_q_len);

    for (unsigned long i(q_start); i != q_end; GCS_SM_INCREMENT(i))
    {
        fprintf(file, "%5lu, %d\t", i, sm->wait_q[i].wait);
    }

    fprintf(
        file,
        "\n\nSend monitor history:"
        "\n=====================\n"
        );

    int line(sm->history_line);
    do
    {
        line = (line + 1) % GCS_SM_HIST_LEN;
        fputs(sm->history[line], file);
    }
    while (line != sm->history_line);

    fputs("-----------------------------\n", file);
}

void
gcs_sm_dump_state(gcs_sm_t* sm, FILE* file)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();
    _gcs_sm_dump_state_common(sm, file);
    gu_mutex_unlock (&sm->lock);
}
#endif /* GCS_SM_DEBUG */
