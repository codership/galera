/*
 * Copyright (C) 2008-2013 Codership Oy <info@codership.com>
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

#include <errno.h>

typedef struct gu_fifo gu_fifo_t;

/*! constructor */
extern gu_fifo_t* gu_fifo_create (size_t length, size_t unit);
/*! puts FIFO into closed state, waking up waiting threads */
extern void gu_fifo_close   (gu_fifo_t *queue);
/*! (re)opens FIFO */
extern void gu_fifo_open    (gu_fifo_t *queue);
/*! destructor - would block until all members are dequeued */
extern void gu_fifo_destroy (gu_fifo_t *queue);
/*! for logging purposes */
extern char* gu_fifo_print  (gu_fifo_t *queue);

/*! Lock FIFO */
extern void  gu_fifo_lock      (gu_fifo_t *q);
/*! Release FIFO */
extern void  gu_fifo_release   (gu_fifo_t *q);
/*! Lock FIFO and get pointer to head item
 * @param err contains error code if retval is NULL (otherwise - undefined):
              -ENODATA   - queue closed,
              -ECANCELED - gets were canceled on the queue
 * @retval pointer to head item or NULL if error occured */
extern void* gu_fifo_get_head  (gu_fifo_t* q, int* err);
/*! Advance FIFO head pointer and release FIFO. */
extern void  gu_fifo_pop_head  (gu_fifo_t* q);
/*! Lock FIFO and get pointer to tail item */
extern void* gu_fifo_get_tail  (gu_fifo_t* q);
/*! Advance FIFO tail pointer and release FIFO. */
extern void  gu_fifo_push_tail (gu_fifo_t* q);
/*! Return how many items are in the queue (unprotected) */
extern long  gu_fifo_length    (gu_fifo_t* q);
/*! Return how many items were in the queue on average per push_tail() */
extern void  gu_fifo_stats_get (gu_fifo_t* q, int* q_len, double* q_len_avg);
/*! Flush stats counters */
extern void  gu_fifo_stats_flush(gu_fifo_t* q);

/*! Cancel getters (must be called while holding a FIFO lock) */
extern int gu_fifo_cancel_gets (gu_fifo_t* q);
/*! Resume get operations */
extern int gu_fifo_resume_gets (gu_fifo_t* q);

#endif // _gu_fifo_h_
