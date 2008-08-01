/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Queue object definition (FIFO)
 */

#ifndef _gcs_queue_h_
#define _gcs_queue_h_

#include <pthread.h>

typedef struct gcs_queue_memb
{
    void                  *data;
    struct gcs_queue_memb *next;
}
gcs_queue_memb_t;

typedef struct gcs_queue
{
    gcs_queue_memb_t* head;
    gcs_queue_memb_t* next;
    gcs_queue_memb_t* tail;
    gu_mutex_t        lock;
    gu_cond_t         ready;
//    gu_cond_t    empty;
    int               err;
    size_t            length;
}
gcs_queue_t;

gcs_queue_t *gcs_queue (); /* constructor */

/* Functions below return non-zero as a sign that queue is being destroyed */
#define GCS_QUEUE_QUIT 1

/* append to tail (returns resulting length of the queue) */
int gcs_queue_push      (gcs_queue_t *queue, void *data);
/* pop from queue head (returns remaining length of the queue) */
int gcs_queue_pop       (gcs_queue_t *queue, void **data);
/* pop from queue head, wait if it is empty (returns length of the queue) */
int gcs_queue_pop_wait  (gcs_queue_t *queue, void **data);
/* iterator */
int gcs_queue_next      (gcs_queue_t *queue, void **data);
/* blocking iterator */
int gcs_queue_next_wait (gcs_queue_t *queue, void **data);
/* abort all waiters */
int gcs_queue_abort     (gcs_queue_t *queue);
/* reset error state on queue */
int gcs_queue_reset     (gcs_queue_t *queue);
/* destructor - would block until all members are dequeued */
int gcs_queue_free      (gcs_queue_t *queue);
/* momentary queue lenght */
static inline size_t
gcs_queue_length (gcs_queue_t *queue) { return queue->length; }

#endif // _gcs_queue_h_
