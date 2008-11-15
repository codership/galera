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

#include <pthread.h>
#include <stdio.h>

//#include "gu_assert.h"
//#include "gu_limits.h"
//#include "gu_fifo.h"
#include "galerautils.h"

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
	    if ((array_size * row_size) > mem_limit ||
                (array_len * row_len) > GU_LONG_MAX) {
                return NULL;
            }
        }

        alloc_size = sizeof (gu_fifo_t) + array_size;
        ret = gu_malloc (alloc_size);
        if (ret) {
            memset (ret, 0, alloc_size);
            ret->col_shift   = row_pwr;
            ret->col_mask    = row_len - 1;
            ret->row_shift   = array_pwr;
            ret->row_mask    = array_len - 1;
            ret->length      = row_len * array_len;
            ret->length_mask = ret->length - 1;
            ret->item_size   = item_size;
            ret->row_size    = row_size;
            ret->alloc       = alloc_size;
            gu_mutex_init (&ret->lock, NULL);
            gu_cond_init  (&ret->get_cond, NULL);
            gu_cond_init  (&ret->put_cond, NULL);
	}
    }
    
    return ret;
}

void gu_fifo_close (gu_fifo_t* q)
{
    if (_gu_fifo_lock (q)) {
        gu_fatal ("Failed to lock queue");
        abort();
    }

    if (!q->closed) {
        q->closed = true;
        gu_cond_broadcast (&q->put_cond);
        gu_cond_broadcast (&q->get_cond);
    }

    gu_fifo_release (q);
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
        long row = _GU_FIFO_ROW(queue,queue->tail);
        if (queue->rows[row]) gu_free (queue->rows[row]);
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
	      queue->row_mask + 1,
	      queue->col_mask + 1,
	      queue->used, queue->used * queue->item_size,
	      queue->alloc,
              queue->head, queue->tail
              //, queue->next
	);
    
    ret = strdup (tmp);
    return ret;
}
