/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * FIFO "class"
 * Implements fixed size "mallocless" FIFO (read "ring buffer").
 * Except gcs_fifo_create() there are two types of fifo
 * access methods - protected and unprotected. Unprotected
 * methods assume that calling routines implement their own
 * protection, and thus are simplified for speed.
 */

#ifndef _GCS_FIFO_H_
#define _GCS_FIFO_H_

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <galerautils.h>

#include "gcs.h"
//#include "gcs_utils.h"

typedef enum gcs_fifo_safe
{
    GCS_FIFO_UNSAFE = 0,
    GCS_FIFO_SAFE   = 1
}
gcs_fifo_safe_t;

typedef struct gcs_fifo
{
    const void* *fifo;
    size_t       length;
    size_t       mask;
    size_t       head;
    size_t       tail;
    size_t       used;
    int          destroyed;
    gu_mutex_t   busy_lock;
    gu_cond_t    free_signal;
}
gcs_fifo_t;

/* Creates FIFO object. Since it practically consists of array of (void*),
 * the length can be chosen arbitrarily high - to minimize the risk
 * of overflow situation.
 */
gcs_fifo_t* gcs_fifo_create  (const size_t length);
int         gcs_fifo_destroy (gcs_fifo_t** fifo);

static inline
int gcs_fifo_put (gcs_fifo_t* const fifo, const void* const item)
{
    assert (fifo && item);
    if (fifo->used < fifo->length) {
	fifo->fifo[fifo->tail] = item;
	fifo->tail = (fifo->tail + 1) & fifo->mask; /* round robin */
	fifo->used++;
	return 0;
    }
    else {
	return -EAGAIN;
    }
}

/*! Removes the last put item from fifo */
static inline
int gcs_fifo_remove (gcs_fifo_t* const fifo)
{
    assert (fifo);
    if (fifo->used) {
	fifo->tail = (fifo->tail - 1) & fifo->mask; /* round robin */
	fifo->fifo[fifo->tail] = NULL;
	fifo->used--;
	return 0;
    }
    else {
	return -ENODATA;
    }
}

static inline
void* gcs_fifo_get (gcs_fifo_t* const fifo)
{
    if (fifo->used) {
	register const void* ret;
	ret = fifo->fifo[fifo->head];
	fifo->head = (fifo->head + 1) & fifo->mask;
	fifo->used--;
	return (void *) ret;
    }
    else {
	return NULL;
    }
}

static inline
void* gcs_fifo_head (gcs_fifo_t* const fifo)
{
    if (fifo->used) {
	return (void *) fifo->fifo[fifo->head];
    }
    else {
	return NULL;
    }
}

int gcs_fifo_safe_destroy (gcs_fifo_t** fifo);

static inline
int gcs_fifo_safe_put (gcs_fifo_t* const fifo, const void* const item)
{
    assert (fifo && item);

    if (gu_mutex_lock (&fifo->busy_lock) || fifo->destroyed) {
	return -ECANCELED;
    }
    
    while (fifo->used >= fifo->length) {
	gu_cond_wait (&fifo->free_signal, &fifo->busy_lock);
	if (fifo->destroyed) return -ECANCELED;
    }
    
    fifo->fifo[fifo->tail] = item;
    fifo->tail = (fifo->tail + 1) & fifo->mask;
    fifo->used++;
    
    if (1 == fifo->used) {
	/* before this FIFO was empty, there could be someone waiting */
	gu_cond_broadcast (&fifo->free_signal);
    }
    
    gu_mutex_unlock (&fifo->busy_lock);
    return 0;
}

static inline
int gcs_fifo_safe_remove (gcs_fifo_t* const fifo)
{
    int ret;
    assert (fifo);

    if (gu_mutex_lock (&fifo->busy_lock) || fifo->destroyed) {
	return -ECANCELED;
    }
    
    if (fifo->used) {    
        fifo->tail = (fifo->tail - 1) & fifo->mask;
        fifo->fifo[fifo->tail] = NULL;
        fifo->used--;
        ret = 0;
    }
    else {
        ret = -ENODATA;
    }
    
    gu_mutex_unlock (&fifo->busy_lock);
    return ret;
}

static inline
void* gcs_fifo_safe_get (gcs_fifo_t* const fifo)
{
    register const void* ret;

    assert (fifo);

    if (gu_mutex_lock (&fifo->busy_lock)) {
	return NULL;
    }
    
    while (0 == fifo->used) {
	if (fifo->destroyed) return NULL;
	gu_cond_wait (&fifo->free_signal, &fifo->busy_lock);
    }
    
    ret = fifo->fifo[fifo->head];
    fifo->head = (fifo->head + 1) & fifo->mask;
    fifo->used--;
    
    if (1 == (fifo->length - fifo->used)) {
	gu_cond_broadcast (&fifo->free_signal);
    }
    
    gu_mutex_unlock (&fifo->busy_lock);
    return (void *) ret;
}

static inline
void* gcs_fifo_safe_head (gcs_fifo_t* const fifo)
{
    register const void* ret;

    assert (fifo);

    if (gu_mutex_lock (&fifo->busy_lock)) {
	return NULL;
    }
    
    if (fifo->used) {
	ret = fifo->fifo[fifo->head];
    }
    else {
	ret = NULL;
    }
    
    gu_mutex_unlock (&fifo->busy_lock);
    return (void *) ret;
}

#endif /* _GCS_FIFO_H_ */
