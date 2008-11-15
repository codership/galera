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
#include "gu_log.h"

struct gu_fifo
{
    ulong col_shift;
    ulong col_mask;
    ulong row_shift;
    ulong row_mask;
    ulong item_size;
    ulong head;
    ulong tail;
    ulong row_size;
    ulong length;
    ulong length_mask;
    ulong used;
    ulong alloc;
    long  get_wait;
    long  put_wait;
    bool  closed;
    
    gu_mutex_t   lock;
    gu_cond_t    get_cond;
    gu_cond_t    put_cond;

    void* rows[];
};
typedef struct gu_fifo gu_fifo_t;

/*! constructor */
gu_fifo_t *gu_fifo_create (size_t length, size_t unit);
/*! puts FIFO into clise state */
void  gu_fifo_close (gu_fifo_t *queue);
/*! destructor - would block until all members are dequeued */
void  gu_fifo_destroy (gu_fifo_t *queue);
/*! for logging purposes */
char *gu_fifo_print (gu_fifo_t *queue);

/*! Release FIFO */
static inline long  gu_fifo_release   (gu_fifo_t *q);
/*! Lock FIFO and get pointer to head item */
static inline void* gu_fifo_get_head  (gu_fifo_t* q);
/*! Advance FIFO head pointer and release FIFO. */
static inline void  gu_fifo_pop_head  (gu_fifo_t* q);
/*! Lock FIFO and get pointer to tail item */
static inline void* gu_fifo_get_tail  (gu_fifo_t* q);
/*! Advance FIFO tail pointer and release FIFO. */
static inline void  gu_fifo_push_tail (gu_fifo_t* q);
/*! Return how many items are in the queue */
static inline ulong gu_fifo_length    (gu_fifo_t* q);

//////////////////// Nothing usable below /////////////////////////////

/* lock the queue */
static inline long _gu_fifo_lock      (gu_fifo_t *q)
{
    return -(gu_mutex_lock (&q->lock));
}

/* unlock the queue */
static inline long gu_fifo_release    (gu_fifo_t *q)
{
    return -(gu_mutex_unlock (&q->lock));
}

/* lock the queue and wait if it is empty */
static inline long _gu_fifo_lock_get (gu_fifo_t *q)
{
    register long ret = _gu_fifo_lock (q);

    while (0 == ret && 0 == q->used && !q->closed) {
        q->get_wait++;
        ret = gu_cond_wait (&q->get_cond, &q->lock);
    }
 
    return ret;
}

/* unlock the queue after getting item */
static inline long _gu_fifo_unlock_get (gu_fifo_t *q)
{
    assert (q->used < q->length);

    if (q->put_wait > 0) {
        q->put_wait--;
        gu_cond_signal (&q->put_cond);
    }

    return gu_fifo_release (q);
}

/* lock the queue and wait if it is full */
static inline long _gu_fifo_lock_put (gu_fifo_t *q)
{
    register long ret = _gu_fifo_lock (q);

    while (0 == ret && q->used == q->length && !q->closed) {
        q->put_wait++;
        ret = gu_cond_wait (&q->put_cond, &q->lock);
    }

    return ret;
}

/* unlock the queue after putting item */
static inline long _gu_fifo_unlock_put (gu_fifo_t *q)
{
    assert (q->used > 0);

    if (q->get_wait > 0) {
        q->get_wait--;
        gu_cond_signal (&q->get_cond);
    }

    return gu_fifo_release (q);
}

#define _GU_FIFO_ROW(q,x) ((x) >> (q)->col_shift) /* div by row width */
#define _GU_FIFO_COL(q,x) ((x) &  (q)->col_mask)  /* remnant */
#define _GU_FIFO_PTR(q,x) ((q)->rows[_GU_FIFO_ROW(q, x)] +      \
                           _GU_FIFO_COL(q, x) * (q)->item_size)

/* Increment and roll over */
#define _GU_FIFO_INC(q,x) (((x) + 1) & (q)->length_mask)


/*! If FIFO is not empty, returns pointer to the head item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
static inline void*
gu_fifo_get_head (gu_fifo_t* q)
{
    if (_gu_fifo_lock_get (q)) {
        gu_fatal ("Faled to lock queue to get item.");
        abort();
    }

    if (gu_likely(q->used)) { // keep fetching items even if closed, until 0
        return (_GU_FIFO_PTR(q, q->head));
    }
    else {
        assert (q->closed);
        gu_fifo_release (q);
        return NULL;
    }
}

/*! Advances FIFO head and unlocks FIFO. */
static inline void
gu_fifo_pop_head (gu_fifo_t* q)
{
    if (_GU_FIFO_COL(q, q->head) == q->col_mask) {
        /* removing last unit from the row */
	register ulong row = _GU_FIFO_ROW (q, q->head);
        gu_free (q->rows[row]);
        q->rows[row] = NULL;
        q->alloc -= q->row_size;
    }

    q->head = _GU_FIFO_INC (q, q->head);
    q->used--;

    if (_gu_fifo_unlock_get(q)) {
        gu_fatal ("Faled to unlock queue to get item.");
        abort();
    }
}

/*! If FIFO is not full, returns pointer to the tail item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
static inline void*
gu_fifo_get_tail (gu_fifo_t* q)
{
    if (_gu_fifo_lock_put (q)) {
        gu_fatal ("Faled to lock queue to put item.");
        abort();
    }

    if (gu_likely(!q->closed)) { // stop adding items when closed
	register ulong row = _GU_FIFO_ROW (q, q->tail);
        
        assert (q->used < q->length);

        // check if row is allocated and allocate if not.
	if (NULL == q->rows[row] &&
            NULL == (q->alloc += q->row_size, 
                     q->rows[row] = gu_malloc(q->row_size))) {
            q->alloc -= q->row_size;
        }
        else {
            return (q->rows[row] + _GU_FIFO_COL(q, q->tail) * q->item_size);
        }
    }

    gu_fifo_release (q);
    return NULL;
}

/*! Advances FIFO tail and unlocks FIFO. */
static inline void
gu_fifo_push_tail (gu_fifo_t* q)
{
    q->tail = _GU_FIFO_INC (q, q->tail);
    q->used++;

    if (_gu_fifo_unlock_put(q)) {
        gu_fatal ("Faled to unlock queue to put item.");
        abort();
    }
}

/*! returns how many items are in the queue */
static inline ulong
gu_fifo_length (gu_fifo_t* q)
{
    return q->used;
}

#endif // _gu_fifo_h_
