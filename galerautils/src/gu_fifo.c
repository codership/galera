/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
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

#define _BSD_SOURCE

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
    ulong head;
    ulong tail;
    ulong row_size;
    ulong length;
    ulong length_mask;
    ulong alloc;
    long  get_wait;
    long  put_wait;
    long long  q_len;
    long long  q_len_samples;
    uint  item_size;
    uint  used;
    int   get_err;
    bool  closed;

    gu_mutex_t   lock;
    gu_cond_t    get_cond;
    gu_cond_t    put_cond;

    void* rows[];
};

/* Don't make rows less than 1K */
#define GCS_FIFO_MIN_ROW_POWER 10

typedef unsigned long long ull;

/* constructor */
gu_fifo_t *gu_fifo_create (size_t length, size_t item_size)
{
    int row_pwr    = GCS_FIFO_MIN_ROW_POWER;
    ull row_len    = 1 << row_pwr;
    ull row_size   = row_len * item_size;
    int array_pwr  = 1; // need at least 2 rows for alteration
    ull array_len  = 1 << array_pwr;
    ull array_size = array_len * sizeof(void*);
    gu_fifo_t *ret = NULL;

    if (length > 0 && item_size > 0) {
        /* find the best ratio of width and height:
         * the size of a row array must be equal to that of the row */
        while (array_len * row_len < length) {
            if (array_size < row_size) {
                array_pwr++;
                array_len = 1 << array_pwr;
                array_size = array_len * sizeof(void*);
            }
            else {
                row_pwr++;
                row_len = 1 << row_pwr;
                row_size = row_len * item_size;
            }
        }

        ull alloc_size = array_size + sizeof (gu_fifo_t);

        if (alloc_size > (size_t)-1) {
            gu_error ("Initial FIFO size %llu exceeds size_t range %zu",
                      alloc_size, (size_t)-1);
            return NULL;
        }

        ull max_size = array_len * row_size + alloc_size;

        if (max_size > (size_t)-1) {
            gu_error ("Maximum FIFO size %llu exceeds size_t range %zu",
                      max_size, (size_t)-1);
            return NULL;
        }

        if (max_size > gu_avphys_bytes()) {
            gu_error ("Maximum FIFO size %llu exceeds available memory "
                      "limit %llu", max_size, gu_avphys_bytes());
            return NULL;
        }

        if ((array_len * row_len) > (ull)GU_LONG_MAX) {
            gu_error ("Resulting queue length %llu exceeds max allowed %ld",
                      array_len * row_len, GU_LONG_MAX);
            return NULL;
        }


        gu_debug ("Creating FIFO buffer of %llu elements of size %llu, "
                  "memory min used: %zu, max used: %zu",
                  array_len * row_len, item_size, alloc_size,
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

// defined as macro for proper line reporting
#define fifo_lock(q)                                    \
    if (gu_likely (0 == gu_mutex_lock (&q->lock))) {}   \
    else {                                              \
        gu_fatal ("Failed to lock queue");              \
        abort();                                        \
    }

static inline int
fifo_unlock (gu_fifo_t* q)
{
    return -gu_mutex_unlock (&q->lock);
}

/* lock the queue */
void gu_fifo_lock    (gu_fifo_t *q)
{
    fifo_lock(q);
}

/* unlock the queue */
void gu_fifo_release (gu_fifo_t *q)
{
    fifo_unlock(q);
}

static int fifo_flush (gu_fifo_t* q)
{
    int ret = 0;

    /* if there are items in the queue, wait until they are all fetched */
    while (q->used > 0 && 0 == ret) {
        /* will make getters to signal every time item is removed */
        gu_warn ("Waiting for %lu items to be fetched.", q->used);
        q->put_wait++;
        ret = gu_cond_wait (&q->put_cond, &q->lock);
    }

    return ret;
}

static void fifo_close (gu_fifo_t* q)
{
    if (!q->closed) {

        q->closed  = true; /* force putters to quit */

        /* don't overwrite existing get_err status, see gu_fifo_resume_gets() */
        if (!q->get_err) q->get_err = -ENODATA;

        // signal all the idle waiting threads
        gu_cond_broadcast (&q->put_cond);
        q->put_wait = 0;
        gu_cond_broadcast (&q->get_cond);
        q->get_wait = 0;

#if 0
        (void) fifo_flush (q);
#endif
    }
}

void gu_fifo_close (gu_fifo_t* q)
{
    fifo_lock   (q);
    fifo_close  (q);
    fifo_unlock (q);
}

void gu_fifo_open (gu_fifo_t* q)
{
    fifo_lock   (q);
    q->closed  = false;
    q->get_err = 0;
    fifo_unlock (q);
}

/* lock the queue and wait if it is empty */
static inline int fifo_lock_get (gu_fifo_t *q)
{
    int ret = 0;

    fifo_lock(q);

    while (0 == ret && !(ret = q->get_err) && 0 == q->used) {
        q->get_wait++;
        ret = -gu_cond_wait (&q->get_cond, &q->lock);
    }

    return ret;
}

/* unlock the queue after getting item */
static inline int fifo_unlock_get (gu_fifo_t *q)
{
    assert (q->used < q->length || 0 == q->length);

    if (q->put_wait > 0) {
        q->put_wait--;
        gu_cond_signal (&q->put_cond);
    }

    return fifo_unlock(q);
}

/* lock the queue and wait if it is full */
static inline int fifo_lock_put (gu_fifo_t *q)
{
    int ret = 0;

    fifo_lock(q);
    while (0 == ret && q->used == q->length && !q->closed) {
        q->put_wait++;
        ret = -gu_cond_wait (&q->put_cond, &q->lock);
    }

    return ret;
}

/* unlock the queue after putting an item */
static inline int fifo_unlock_put (gu_fifo_t *q)
{
    assert (q->used > 0);

    if (q->get_wait > 0) {
        q->get_wait--;
        gu_cond_signal (&q->get_cond);
    }

    return fifo_unlock(q);
}

#define FIFO_ROW(q,x) ((x) >> q->col_shift) /* div by row width */
#define FIFO_COL(q,x) ((x) &  q->col_mask)  /* remnant */
#define FIFO_PTR(q,x) \
    ((uint8_t*)q->rows[FIFO_ROW(q, x)] + FIFO_COL(q, x) * q->item_size)

/* Increment and roll over */
#define FIFO_INC(q,x) (((x) + 1) & q->length_mask)


/*! If FIFO is not empty, returns pointer to the head item and locks FIFO,
 *  otherwise blocks. Or returns NULL if FIFO is closed. */
void* gu_fifo_get_head (gu_fifo_t* q, int* err)
{
    *err = fifo_lock_get (q);

    if (gu_likely(-ECANCELED != *err && q->used)) {
        return (FIFO_PTR(q, q->head));
    }
    else {
        assert (q->get_err);
        fifo_unlock (q);
        return NULL;
    }
}

/*! Advances FIFO head and unlocks FIFO. */
void gu_fifo_pop_head (gu_fifo_t* q)
{
    if (FIFO_COL(q, q->head) == q->col_mask) {
        /* removing last unit from the row */
        ulong row = FIFO_ROW (q, q->head);
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
    fifo_lock_put (q);

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
            return ((uint8_t*)q->rows[row] +
                    FIFO_COL(q, q->tail) * q->item_size);
        }
#if 0 // for debugging
        if (NULL == q->rows[row]) {
            gu_debug ("Allocating row %lu of queue %p, rows %p",
                      row, q, q->rows);
            if (NULL == (q->rows[row] = gu_malloc(q->row_size))) {
                gu_debug ("Allocating row %lu failed", row);
                fifo_unlock (q);
                return NULL;
            }
            q->alloc += q->row_size;
        }
        return (q->rows[row] + FIFO_COL(q, q->tail) * q->item_size);
#endif
    }

    fifo_unlock (q);
    return NULL;
}

/*! Advances FIFO tail and unlocks FIFO. */
void gu_fifo_push_tail (gu_fifo_t* q)
{
    q->tail = FIFO_INC(q, q->tail);
    q->q_len += q->used;
    q->used++;
    q->q_len_samples++;

    if (fifo_unlock_put(q)) {
        gu_fatal ("Faled to unlock queue to put item.");
        abort();
    }
}

/*! returns how many items are in the queue */
long gu_fifo_length (gu_fifo_t* q)
{
    return q->used;
}

/*! returns how many items were in the queue per push_tail() */
void gu_fifo_stats_get (gu_fifo_t* q, int* q_len, double* q_len_avg)
{
    fifo_lock (q);

    *q_len = q->used;

    long long len     = q->q_len;
    long long samples = q->q_len_samples;

    fifo_unlock (q);

    if (len >= 0 && samples >= 0) {
        if (samples > 0)
        {
            *q_len_avg = ((double)len) / samples;
        }
        else
        {
            assert (0 == len);
            *q_len_avg = 0.0;
        }
    }
    else {
        *q_len_avg = -1.0;
    }
}

void gu_fifo_stats_flush(gu_fifo_t* q)
{
    fifo_lock (q);

    q->q_len = 0;
    q->q_len_samples = 0;

    fifo_unlock (q);
}

/* destructor - would block until all members are dequeued */
void gu_fifo_destroy   (gu_fifo_t *queue)
{
    fifo_lock (queue);
    {
        if (!queue->closed) fifo_close(queue);

        fifo_flush (queue);
    }
    fifo_unlock (queue);

    assert (queue->tail == queue->head);

    while (gu_cond_destroy (&queue->put_cond)) {
        fifo_lock      (queue);
        gu_cond_signal (&queue->put_cond);
        fifo_unlock    (queue);
        /* when thread sees that ret->used == 0, it must terminate */
    }

    while (gu_cond_destroy (&queue->get_cond)) {
        fifo_lock      (queue);
        gu_cond_signal (&queue->get_cond);
        fifo_unlock    (queue);
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
              "\tused    = %u (%zu bytes)\n"
              "\talloctd = %lu bytes\n"
              "\thead    = %lu, tail = %lu\n"
              "\tavg.len = %f"
              //", next = %lu"
              ,
              (void*)queue,
              queue->length,
              queue->rows_num,
              queue->col_mask + 1,
              queue->used, (size_t)queue->used * queue->item_size,
              queue->alloc,
              queue->head, queue->tail,
              queue->q_len_samples > 0 ?
              ((double)queue->q_len)/queue->q_len_samples : 0.0
              //, queue->next
        );

    ret = strdup (tmp);
    return ret;
}

int
gu_fifo_cancel_gets (gu_fifo_t* q)
{
    if (q->get_err && -ENODATA != q->get_err) {
        gu_error ("Attempt to cancel FIFO gets in state: %d (%s)",
                  q->get_err, strerror(-q->get_err));
        return -EBADFD;
    }

    assert (!q->get_err || q->closed);

    q->get_err = -ECANCELED; /* force getters to quit with specific error */

    if (q->get_wait) {
        gu_cond_broadcast (&q->get_cond);
        q->get_wait = 0;
    }

    return 0;
}

int
gu_fifo_resume_gets (gu_fifo_t* q)
{
    int ret = -1;

    fifo_lock(q);

    if (-ECANCELED == q->get_err) {
        q->get_err = q->closed ? -ENODATA : 0;
        ret = 0;
    }
    else {
        gu_error ("Attempt to resume FIFO gets in state: %d (%s)",
                  q->get_err, strerror(-q->get_err));
        ret = -EBADFD;
    }

    fifo_unlock(q);

    return ret;
}
