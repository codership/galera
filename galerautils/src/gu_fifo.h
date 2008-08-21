/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * Queue (FIFO) class definition
 *
 * The driving idea behind this class is avoiding malloc()'s
 * at all costs on one hand, on the other - make it almost
 * as infinite as an ordinary linked list. FIFO properties
 * help to achieve that.
 *
 * When needed this FIFO can be made very big, holding
 * millions or even billions of items while taking up
 * minimum space when there are few items in the queue.
 * malloc()'s do happen, but once per thousand of pushes and
 * allocate multiples of pages, thus reducing memory fragmentation.
 */

#ifndef _gu_fifo_h_
#define _gu_fifo_h_

#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "gu_mem.h"
#include "gu_mutex.h"

struct gu_fifo
{
    ulong col_shift;
    ulong col_mask;
    ulong row_shift;
    ulong row_mask;
    ulong item_size;
    ulong head;
    ulong tail;
    ulong next;
    ulong row_length;
    ulong row_size;
    ulong length;
    ulong length_mask;
    ulong used;
    ulong alloc;
    ulong waiting;
    bool  next_stop;
    
    gu_mutex_t   lock;
    gu_cond_t    ready;
    long         err;

    void* rows[];
};
typedef struct gu_fifo gu_fifo_t;

#define GU_FIFO_ROW(q,x) ((x) >> (q->col_shift)) /* div by row width */
#define GU_FIFO_COL(q,x) ((x) &  (q->col_mask))  /* remnant */
#define GU_FIFO_ADD(q,x) (((x) + 1) & (q->length_mask))

/* constructor */
gu_fifo_t *gu_fifo_create (size_t length, size_t unit);
/* destructor - would block until all members are dequeued */
long  gu_fifo_destroy (gu_fifo_t *queue);
/* for logging purposes */
char *gu_fifo_print (gu_fifo_t *queue);

/* lock the queue */
static inline long gu_fifo_lock      (gu_fifo_t *q);
/* lock the queue and wait if it is empty */
static inline long gu_fifo_lock_wait (gu_fifo_t *q);
/* signal to waiting thread */
static inline void gu_fifo_signal    (gu_fifo_t *q);
/* unlock the queue */
static inline long gu_fifo_unlock    (gu_fifo_t *q);
/* append to tail */
static inline long gu_fifo_push      (gu_fifo_t *q, void *data);
/* append to tail, under lock */
static inline long gu_fifo_push_lock (gu_fifo_t *q, void *data);
/* pop from queue head */
static inline long gu_fifo_pop       (gu_fifo_t *q, void *data);
/* pop from queue head, wait if it is empty */
static inline long gu_fifo_pop_wait  (gu_fifo_t *q, void *data);
/* iterator */
static inline long gu_fifo_next      (gu_fifo_t *q, void *data);
/* blocking iterator */
static inline long gu_fifo_next_wait (gu_fifo_t *q, void *data);
/* reset iterator to queue head */
static inline long gu_fifo_next_reset (gu_fifo_t *q);

/* lock the queue */
static inline long gu_fifo_lock      (gu_fifo_t *q)
{
    return -(gu_mutex_lock (&q->lock));
}

/* lock the queue and wait if it is empty */
static inline long gu_fifo_lock_wait (gu_fifo_t *q)
{
    register long ret = gu_fifo_lock (q);
    if (0 == ret && 0 == q->used) {
        q->waiting++;
        ret = gu_cond_wait (&q->ready, &q->lock);
        if (0 == ret && 0 == q->used) {
            // mutex locked but no data
            gu_mutex_unlock (&q->lock);
            ret = -EINTR;
        }
    }
    return ret;
}

/* unlock the queue */
static inline long gu_fifo_unlock    (gu_fifo_t *q)
{
    return -(gu_mutex_unlock (&q->lock));
}

/* signal to waiting thread */
static inline void gu_fifo_signal    (gu_fifo_t *q)
{
    if (q->waiting > 0) {
        // FIFO was empty and somebody's waiting, signal
        q->waiting--;
        gu_cond_signal (&q->ready);
    }
}

/* append to tail */
static inline long gu_fifo_push      (gu_fifo_t *q, void *data)
{
    if (q->used < q->length) {
	register ulong row = GU_FIFO_ROW (q, q->tail);
	if (NULL == q->rows[row] &&
            NULL == (q->alloc += q->row_size, 
                     q->rows[row] = gu_malloc(q->row_size))) {
            q->alloc -= q->row_size;
            return -ENOMEM;
        }
	memcpy (q->rows[row] + GU_FIFO_COL(q, q->tail) * q->item_size,
		data, q->item_size);
	q->tail = GU_FIFO_ADD (q, q->tail);
	q->used++;
	return q->used;
    }
    return -EAGAIN;
}

/* append to tail, under lock */
static inline long gu_fifo_push_lock (gu_fifo_t *q, void *data)
{
    register long ret = gu_fifo_lock (q);
    if (0 == ret) {
        ret = gu_fifo_push (q, data);
        gu_fifo_signal (q);
        gu_fifo_unlock (q);
    }
    return ret;
}

/* pop from queue head */
static inline long gu_fifo_pop       (gu_fifo_t *q, void *data)
{
    if (q->used > 0) {
	register ulong row = GU_FIFO_ROW (q, q->head);
	memcpy (data,
		q->rows[row] + GU_FIFO_COL(q, q->head) * q->item_size,
		q->item_size);
	q->head = GU_FIFO_ADD (q, q->head);
	q->used--;
	if (0 == GU_FIFO_COL (q, q->head)) {
	    /* removed last unit from the row */
	    gu_free (q->rows[row]);
            q->rows[row] = NULL;
	    q->alloc -= q->row_size;
	}
	return q->used;
    }
    return -EAGAIN;
}

/* pop from queue head, wait if it is empty */
static inline long gu_fifo_pop_wait  (gu_fifo_t *q, void *data)
{
    register long ret;
    if (!(ret = gu_fifo_lock_wait (q))) {
	ret = gu_fifo_pop (q, data);
	gu_fifo_unlock (q);
    }
    return ret;
}

/* iterator */
static inline long gu_fifo_next      (gu_fifo_t *q, void *data)
{
    if (q->used > 0 && !q->next_stop) {
	register size_t row = GU_FIFO_ROW (q, q->next);
	memcpy (data,
		q->rows[row] + GU_FIFO_COL(q, q->next) * q->item_size,
		q->item_size);
	q->next = GU_FIFO_ADD (q, q->next);
        q->next_stop = (q->next == q->tail); // reached end
	return 0;
    }
    return -EAGAIN;
}

/* blocking iterator */
static inline long gu_fifo_next_wait (gu_fifo_t *q, void *data)
{
    register long ret;
    if (!(ret = gu_fifo_lock_wait (q))) {
	ret = gu_fifo_next (q, data);
	gu_fifo_unlock (q);
    }
    return ret;    
}

/* reset iterator to queue head */
static inline long gu_fifo_next_reset (gu_fifo_t *q)
{
    q->next      = q->head;
    q->next_stop = false;
    return 0;
}

#endif // _gu_fifo_h_
