/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file GCS Send Monitor. To ensure fair (FIFO) access to gcs_core_send()
 */

#include "gcs_sm.h"

#include <errno.h>
#include <string.h>

extern gcs_sm_t*
gcs_sm_create (unsigned long len)
{
    size_t sm_size = sizeof(gcs_sm_t) + len * sizeof(pthread_cond_t*);
    gcs_sm_t* sm = gu_malloc(sm_size);

    if (sm) {
        pthread_mutex_init (&sm->lock, NULL);
        sm->wait_q_size = len;
        sm->wait_q_mask = sm->wait_q_size - 1;
        sm->wait_q_head = 0;
        sm->wait_q_len  = -1; // -n where n is a number of simult. users
        sm->ret         = 0;
        memset (sm->wait_q, 0, sm->wait_q_size * sizeof(pthread_cond_t*));
    }

    return sm;
}

extern long
gcs_sm_close (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->ret = -EBADFD;

    sm->wait_q_len++;

    if (sm->wait_q_len > 0) { // wait for cleared queue
        pthread_cond_t cond;
        pthread_cond_init (&cond, NULL);

        _gcs_sm_enqueue_unsafe (sm, &cond);

        pthread_cond_destroy (&cond);
    }

    sm->wait_q_len--;

    gu_mutex_unlock (&sm->lock);

    return 0;
}

extern void
gcs_sm_destroy (gcs_sm_t* sm)
{
    pthread_mutex_destroy(&sm->lock);
    gu_free (sm);
}

