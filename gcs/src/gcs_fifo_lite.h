/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
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

#include "gcs.h"

typedef struct gcs_fifo_lite
{
    ulong       length;
    ulong       item_size;
    ulong       mask;
    ulong       head;
    ulong       tail;
    ulong       used;
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
long             gcs_fifo_lite_close   (gcs_fifo_lite_t* fifo);
long             gcs_fifo_lite_destroy (gcs_fifo_lite_t* fifo);

static inline void*
_gcs_fifo_lite_tail (gcs_fifo_lite_t* f)
{
    return (f->queue + f->tail * f->item_size);
}

static inline void*
_gcs_fifo_lite_head (gcs_fifo_lite_t* f)
{
    return (f->queue + f->head * f->item_size);
}

static inline long
gcs_fifo_lite_lock (gcs_fifo_lite_t* fifo)
{
    return -gu_mutex_lock (&fifo->lock);
}

static inline long
gcs_fifo_lite_unlock (gcs_fifo_lite_t* fifo)
{
    return -gu_mutex_unlock (&fifo->lock);
}

static inline long
gcs_fifo_lite_wait_put (gcs_fifo_lite_t* fifo)
{
    register long ret = gcs_fifo_lite_lock (fifo);
    if (gu_likely(!ret)) {
        while (!fifo->closed && fifo->used >= fifo->length) {
            fifo->put_wait++;
            gu_cond_wait (&fifo->put_cond, &fifo->lock);
        }
        if (gu_unlikely(fifo->closed)) ret = -ECANCELED;
    }
    return ret;
}

static inline long
gcs_fifo_lite_wait_get (gcs_fifo_lite_t* fifo)
{
    register long ret = gcs_fifo_lite_lock (fifo);
    if (gu_likely(!ret)) {
        while (!fifo->closed && 0 == fifo->used) {
            fifo->get_wait++;
            gu_cond_wait (&fifo->get_cond, &fifo->lock);
        }
        if (gu_unlikely(fifo->closed && 0 == fifo->used)) ret = -ECANCELED;
    }
    return ret;
}

static inline long
gcs_fifo_lite_signal_put (gcs_fifo_lite_t* fifo)
{
    if (fifo->put_wait > 0) {
        fifo->put_wait--;
        gu_cond_signal (&fifo->put_cond);
    }
    return (gcs_fifo_lite_unlock (fifo));
}

static inline long
gcs_fifo_lite_signal_get (gcs_fifo_lite_t* fifo)
{
    if (fifo->get_wait > 0) {
        fifo->get_wait--;
        gu_cond_signal (&fifo->get_cond);
    }
    return (gcs_fifo_lite_unlock (fifo));
}

static inline long
gcs_fifo_lite_put (gcs_fifo_lite_t* fifo, const void* item)
{
    long ret;

    assert (fifo && item);

    if (!(ret = gcs_fifo_lite_wait_put (fifo))) {
        memcpy (_gcs_fifo_lite_tail(fifo), item, fifo->item_size);
        fifo->tail = (fifo->tail + 1) & fifo->mask;
        ret = ++fifo->used;
        gcs_fifo_lite_signal_get (fifo);
    }    
    return ret;
}

static inline long
gcs_fifo_lite_remove (gcs_fifo_lite_t* const fifo)
{
    assert (fifo);

    if (gu_mutex_lock (&fifo->lock)) {
	return -ECANCELED;
    }
    
    if (fifo->used) {    
        fifo->tail = (fifo->tail - 1) & fifo->mask;
        fifo->used--;
    }
    else {
        assert (0);
        abort();
    }
    gcs_fifo_lite_signal_put (fifo);
    return fifo->used;
}

static inline long
gcs_fifo_lite_get (gcs_fifo_lite_t* const fifo, void* item)
{
    long ret;

    assert (fifo && item);

    if (!(ret = gcs_fifo_lite_wait_get (fifo))) {
        memcpy (item, _gcs_fifo_lite_head (fifo), fifo->item_size);
        fifo->head = (fifo->head + 1) & fifo->mask;
        ret = --fifo->used;
        gcs_fifo_lite_signal_put (fifo);
    }
    return ret;
}

static inline long
gcs_fifo_lite_head (gcs_fifo_lite_t* const fifo, void* item)
{
    long ret;

    assert (fifo && item);

    if (!(ret = gcs_fifo_lite_wait_get (fifo))) {
        memcpy (item, _gcs_fifo_lite_head (fifo), fifo->item_size);
        ret = fifo->used;
    }
    return ret;
}

#endif /* _GCS_FIFO_LITE_H_ */
