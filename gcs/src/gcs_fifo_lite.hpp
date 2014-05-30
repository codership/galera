/*
 * Copyright (C) 2008-2011 Codership Oy <info@codership.com>
 *
 * $Id$
 *
 * FIFO "class" customized for particular purpose
 * (here I decided to sacrifice generality for efficiency).
 * Implements fixed size "mallocless" FIFO (read "ring buffer").
 * Except gcs_fifo_create() there are two types of fifo
 * access methods - protected and unprotected. Unprotected
 * methods assume that calling routines implement their own
 * protection, and thus are simplified for speed.
 */

#ifndef _GCS_FIFO_LITE_H_
#define _GCS_FIFO_LITE_H_

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include <galerautils.h>

#include "gcs.hpp"

typedef struct gcs_fifo_lite
{
    long        length;
    ulong       item_size;
    ulong       mask;
    ulong       head;
    ulong       tail;
    long        used;
    bool        closed;
    bool        destroyed;
    long        put_wait;
    long        get_wait;
    gu_cond_t   put_cond;
    gu_cond_t   get_cond;
    gu_mutex_t  lock;
    void*       queue;
}
gcs_fifo_lite_t;

/* Creates FIFO object. Since it practically consists of array of (void*),
 * the length can be chosen arbitrarily high - to minimize the risk
 * of overflow situation.
 */
gcs_fifo_lite_t* gcs_fifo_lite_create  (size_t length, size_t item_size);
void             gcs_fifo_lite_close   (gcs_fifo_lite_t* fifo);
void             gcs_fifo_lite_open    (gcs_fifo_lite_t* fifo);
long             gcs_fifo_lite_destroy (gcs_fifo_lite_t* fifo);

static inline void*
_gcs_fifo_lite_tail (gcs_fifo_lite_t* f)
{
    return ((char*)f->queue + f->tail * f->item_size);
}

static inline void*
_gcs_fifo_lite_head (gcs_fifo_lite_t* f)
{
    return ((char*)f->queue + f->head * f->item_size);
}

#define GCS_FIFO_LITE_LOCK                                              \
    if (gu_unlikely (gu_mutex_lock (&fifo->lock))) {                    \
        gu_fatal ("Mutex lock failed.");                                \
        abort();                                                        \
    }

/*! If FIFO is not full, returns pointer to the tail item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
static inline void*
gcs_fifo_lite_get_tail (gcs_fifo_lite_t* fifo)
{
    void* ret = NULL;

    GCS_FIFO_LITE_LOCK;

    while (!fifo->closed && fifo->used >= fifo->length) {
        fifo->put_wait++;
        gu_cond_wait (&fifo->put_cond, &fifo->lock);
    }

    if (gu_likely(!fifo->closed)) {
        assert (fifo->used < fifo->length);
        ret = _gcs_fifo_lite_tail (fifo);
    }
    else {
        gu_mutex_unlock (&fifo->lock);
    }

    return ret;
}

/*! Advances FIFO tail and unlocks FIFO */
static inline void
gcs_fifo_lite_push_tail (gcs_fifo_lite_t* fifo)
{
    fifo->tail = (fifo->tail + 1) & fifo->mask;
    fifo->used++;

    assert (fifo->used <= fifo->length);

    if (fifo->get_wait > 0) {
        fifo->get_wait--;
        gu_cond_signal (&fifo->get_cond);
    }

    gu_mutex_unlock (&fifo->lock);
}

/*! If FIFO is not empty, returns pointer to the head item and locks FIFO,
 *  or returns NULL if FIFO is empty. Blocking behaviour disabled since
 *  it is not needed in GCS: recv_thread should never block. */
static inline void*
gcs_fifo_lite_get_head (gcs_fifo_lite_t* fifo)
{
    void* ret = NULL;

    GCS_FIFO_LITE_LOCK;

/* Uncomment this for blocking behaviour
   while (!fifo->closed && 0 == fifo->used) {
   fifo->get_wait++;
   gu_cond_wait (&fifo->get_cond, &fifo->lock);
   }
*/
    if (gu_likely(fifo->used > 0)) {
        ret = _gcs_fifo_lite_head (fifo);
    }
    else {
        gu_mutex_unlock (&fifo->lock);
    }

    return ret;
}

/*! Advances FIFO head and unlocks FIFO */
static inline void
gcs_fifo_lite_pop_head (gcs_fifo_lite_t* fifo)
{
    fifo->head = (fifo->head + 1) & fifo->mask;
    fifo->used--;

    assert (fifo->used != -1);

    if (fifo->put_wait > 0) {
        fifo->put_wait--;
        gu_cond_signal (&fifo->put_cond);
    }

    gu_mutex_unlock (&fifo->lock);
}

/*! Unlocks FIFO */
static inline long
gcs_fifo_lite_release (gcs_fifo_lite_t* fifo)
{
    return (gu_mutex_unlock (&fifo->lock));
}

/*! Removes item from tail, returns true if success */
static inline bool
gcs_fifo_lite_remove (gcs_fifo_lite_t* const fifo)
{
    bool ret = false;
    assert (fifo);

    GCS_FIFO_LITE_LOCK;

    if (fifo->used) {
        fifo->tail = (fifo->tail - 1) & fifo->mask;
        fifo->used--;
        ret = true;

        if (fifo->put_wait > 0) {
            fifo->put_wait--;
            gu_cond_signal (&fifo->put_cond);
        }
    }

    gu_mutex_unlock (&fifo->lock);

    return ret;
}

static inline bool
gcs_fifo_lite_not_full (const gcs_fifo_lite_t* const fifo)
{
    return (fifo->used < fifo->length);
}

#endif /* _GCS_FIFO_LITE_H_ */
