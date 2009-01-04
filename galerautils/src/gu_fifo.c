/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * Queue (FIFO) class implementation
 *
 * The driving idea behind this class is avoiding mallocs
 * at all costs on one hand, on the other - make it almost
 * as infinite as an ordinary linked list. FIFO properties
 * help achieving that.
 *
 * When needed this FIFO can be made very big, holding
 * millions or even billions of items while taking up
 * minimum space when there are few items in the queue. 
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "gu_assert.h"
#include "gu_limits.h"
#include "gu_mem.h"
#include "gu_mutex.h"
#include "gu_log.h"
#include "gu_fifo.h"

#include "galerautils.h"

struct gu_fifo
{
    ulong col_shift;
    ulong col_mask;
    ulong rows_num;
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

/* Don't make rows less than 1K */
#define GCS_FIFO_MIN_ROW_POWER 10

/* constructor */
gu_fifo_t *gu_fifo_create (size_t length, size_t item_size)
{
    size_t row_pwr    = GCS_FIFO_MIN_ROW_POWER;
    size_t row_len    = 1 << row_pwr;
    size_t row_size   = row_len * item_size;
    size_t array_pwr  = 0;
    size_t array_len  = 1 << array_pwr;
    size_t array_size = array_len * sizeof(void*);
    size_t mem_limit  = GU_PHYS_PAGES * GU_PAGE_SIZE;
    size_t alloc_size = 0;
    gu_fifo_t *ret    = NULL;

    if (length > 0 && item_size > 0) {
        size_t max_size;
	/* find the best ratio of width and height:
	 * the size of a row array must be equal to that of the row */
	while (array_len * row_len < length) {
            if (array_size < row_size) {
                array_pwr++;
                array_len = 1<< array_pwr;
                array_size = array_len * sizeof(void*);
            }
            else {
                row_pwr++;
                row_len = 1 << row_pwr;
                row_size = row_len * item_size;
            }
        }

        max_size = array_len * row_size + array_size + sizeof(gu_fifo_t);
        if (max_size > mem_limit) {
            gu_error ("Resulting FIFO size %zu exceeds physical memory "
                      "limit %zu", max_size, mem_limit);
            return NULL;
        }
        if ((array_len * row_len) > GU_LONG_MAX) {
            gu_error ("Resulting queue length %zu exceeds max allowed %zu",
                      array_len * row_len, GU_LONG_MAX);
            return NULL;
        }

        alloc_size = sizeof (gu_fifo_t) + array_size;

        gu_debug ("Creating FIFO buffer of %lu elements, memory min used: "
                  "%zu, max used: %zu",
                  array_len * row_len, alloc_size,
                  alloc_size + array_len*row_size);

        ret = gu_malloc (alloc_size);
        if (ret) {
            memset (ret, 0, alloc_size);
            ret->col_shift   = row_pwr;
            ret->col_mask    = row_len - 1;
            ret->rows_num    = array_len;
            ret->length      = row_len * array_len;
            ret->length_mask = ret->length - 1;
            ret->item_size   = item_size;
            ret->row_size    = row_size;
            ret->alloc       = alloc_size;
            gu_mutex_init (&ret->lock, NULL);
            gu_cond_init  (&ret->get_cond, NULL);
            gu_cond_init  (&ret->put_cond, NULL);
	}
        else {
            gu_error ("Failed to allocate %zu bytes for FIFO", alloc_size);
        }
    }
    
    return ret;
}

void gu_fifo_close (gu_fifo_t* q)
{
    if (gu_fifo_lock (q)) {
        gu_fatal ("Failed to lock queue");
        abort();
    }

    if (!q->closed) {
        q->closed = true;
        gu_cond_broadcast (&q->put_cond);
        q->put_wait = 0;
        gu_cond_broadcast (&q->get_cond);
        q->get_wait = 0;
    }

    gu_fifo_release (q);
}

#define FIFO_LOCK(q)   (-gu_mutex_lock   (&q->lock))
#define FIFO_UNLOCK(q) (-gu_mutex_unlock (&q->lock))

/* lock the queue */
long gu_fifo_lock       (gu_fifo_t *q)
{
    return FIFO_LOCK(q);
}

/* unlock the queue */
long gu_fifo_release    (gu_fifo_t *q)
{
    return FIFO_UNLOCK(q);
}

/* lock the queue and wait if it is empty */
static inline long fifo_lock_get (gu_fifo_t *q)
{
    register long ret = FIFO_LOCK(q);

    while (0 == ret && 0 == q->used && !q->closed) {
        q->get_wait++;
        ret = gu_cond_wait (&q->get_cond, &q->lock);
    }
 
    return ret;
}

/* unlock the queue after getting item */
static inline long fifo_unlock_get (gu_fifo_t *q)
{
    assert (q->used < q->length);

    if (q->put_wait > 0) {
        q->put_wait--;
        gu_cond_signal (&q->put_cond);
    }

    return FIFO_UNLOCK(q);
}

/* lock the queue and wait if it is full */
static inline long fifo_lock_put (gu_fifo_t *q)
{
    register long ret = FIFO_LOCK(q);

    while (0 == ret && q->used == q->length && !q->closed) {
        q->put_wait++;
        ret = gu_cond_wait (&q->put_cond, &q->lock);
    }

    return ret;
}

/* unlock the queue after putting item */
static inline long fifo_unlock_put (gu_fifo_t *q)
{
    assert (q->used > 0);

    if (q->get_wait > 0) {
        q->get_wait--;
        gu_cond_signal (&q->get_cond);
    }

    return FIFO_UNLOCK(q);
}

#define FIFO_ROW(q,x) ((x) >> q->col_shift) /* div by row width */
#define FIFO_COL(q,x) ((x) &  q->col_mask)  /* remnant */
#define FIFO_PTR(q,x) (q->rows[FIFO_ROW(q, x)] + FIFO_COL(q, x) * q->item_size)

/* Increment and roll over */
#define FIFO_INC(q,x) (((x) + 1) & q->length_mask)


/*! If FIFO is not empty, returns pointer to the head item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
void* gu_fifo_get_head (gu_fifo_t* q)
{
    if (fifo_lock_get (q)) {
        gu_fatal ("Faled to lock queue to get item.");
        abort();
    }

    if (gu_likely(q->used)) { // keep fetching items even if closed, until 0
        return (FIFO_PTR(q, q->head));
    }
    else {
        assert (q->closed);
        gu_fifo_release (q);
        return NULL;
    }
}

/*! Advances FIFO head and unlocks FIFO. */
void gu_fifo_pop_head (gu_fifo_t* q)
{
    if (FIFO_COL(q, q->head) == q->col_mask) {
        /* removing last unit from the row */
	register ulong row = FIFO_ROW (q, q->head);
        assert (q->rows[row] != NULL);
        gu_free (q->rows[row]);
        q->rows[row] = NULL;
        q->alloc -= q->row_size;
    }

    q->head = FIFO_INC(q, q->head);
    q->used--;

    if (fifo_unlock_get(q)) {
        gu_fatal ("Faled to unlock queue to get item.");
        abort();
    }
}

/*! If FIFO is not full, returns pointer to the tail item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
void* gu_fifo_get_tail (gu_fifo_t* q)
{
    if (fifo_lock_put (q)) {
        gu_fatal ("Faled to lock queue to put item.");
        abort();
    }

    if (gu_likely(!q->closed)) { // stop adding items when closed
	ulong row = FIFO_ROW(q, q->tail);
        
        assert (q->used < q->length);

        // check if row is allocated and allocate if not.
	if (NULL == q->rows[row] &&
            NULL == (q->alloc += q->row_size,
                     q->rows[row] = gu_malloc(q->row_size))) {
            q->alloc -= q->row_size;
        }
        else {
            return (q->rows[row] + FIFO_COL(q, q->tail) * q->item_size);
        }
#if 0 // for debugging
	if (NULL == q->rows[row]) {
            gu_debug ("Allocating row %lu of queue %p, rows %p", row, q, q->rows);
            if (NULL == (q->rows[row] = gu_malloc(q->row_size))) {
                gu_debug ("Allocating row %lu failed", row);
                gu_fifo_release (q);
                return NULL;
            }
            q->alloc += q->row_size;
        }
        return (q->rows[row] + FIFO_COL(q, q->tail) * q->item_size);
#endif
    }

    gu_fifo_release (q);
    return NULL;
}

/*! Advances FIFO tail and unlocks FIFO. */
void gu_fifo_push_tail (gu_fifo_t* q)
{
    q->tail = FIFO_INC(q, q->tail);
    q->used++;

    if (fifo_unlock_put(q)) {
        gu_fatal ("Faled to unlock queue to put item.");
        abort();
    }
}

/*! returns how many items are in the queue */
ulong gu_fifo_length (gu_fifo_t* q)
{
    return q->used;
}

/* destructor - would block until all members are dequeued */
void gu_fifo_destroy   (gu_fifo_t *queue)
{
    gu_mutex_lock (&queue->lock);
    if (0 == queue->length) {
        gu_mutex_unlock (&queue->lock);
    }

    queue->length = 0; /* prevent appending */
    while (queue->used) {
	gu_mutex_unlock (&queue->lock);
	usleep (10000); /* wait until the queue is cleaned */
	gu_mutex_lock (&queue->lock);
    }
    gu_mutex_unlock (&queue->lock);

    assert (queue->tail == queue->head);

    while (gu_cond_destroy (&queue->put_cond)) {
	gu_mutex_lock   (&queue->lock);
	gu_cond_signal  (&queue->put_cond);
	gu_mutex_unlock (&queue->lock);
	/* when thread sees that ret->used == 0, it must terminate */
    }

    while (gu_cond_destroy (&queue->get_cond)) {
	gu_mutex_lock   (&queue->lock);
	gu_cond_signal  (&queue->get_cond);
	gu_mutex_unlock (&queue->lock);
	/* when thread sees that ret->used == 0, it must terminate */
    }

    while (gu_mutex_destroy (&queue->lock)) continue;

    /* only one row migth be left */
    {
        ulong row = FIFO_ROW(queue, queue->tail);
        if (queue->rows[row]) {
            assert (FIFO_COL(queue, queue->tail) != 0);
            gu_free (queue->rows[row]);
            queue->alloc -= queue->row_size;
        }
        else {
            assert (FIFO_COL(queue, queue->tail) == 0);
        }
        gu_free (queue);
    }
}

char *gu_fifo_print (gu_fifo_t *queue)
{
    size_t tmp_len = 4096;
    char tmp[tmp_len];
    char *ret;

    snprintf (tmp, tmp_len,
	      "Queue (%p):\n"
	      "\tlength  = %lu\n"
	      "\trows    = %lu\n"
	      "\tcolumns = %lu\n"
	      "\tused    = %lu (%lu bytes)\n"
	      "\talloctd = %lu bytes\n"
              "\thead    = %lu, tail = %lu"
              //", next = %lu"
              ,
	      queue,
	      queue->length,
	      queue->rows_num,
	      queue->col_mask + 1,
	      queue->used, queue->used * queue->item_size,
	      queue->alloc,
              queue->head, queue->tail
              //, queue->next
	);
    
    ret = strdup (tmp);
    return ret;
}
