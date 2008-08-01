/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/******************************************/
/* Preamble, copyright, bla-bla-bla       */
/* gcs queue object implementation        */
/******************************************/

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#ifdef GCS_DEBUG_QUEUE
#include <string.h>
#endif

#include <galerautils.h>

#include "gcs.h"
#include "gcs_queue.h"

/* This function is installed as a cleanup routine
 * when thread goes waiting on a condition
 * takes pointer to mutex as an argument */
//static void *gcs_queue_cleanup (void *arg)
//{
//    gu_mutex_unlock ((gu_mutex_t *) arg);
//    return NULL;
//}

gcs_queue_t *gcs_queue ()
{
    gcs_queue_t *queue = GU_CALLOC (1, gcs_queue_t);

    if (NULL == queue) return NULL;

    /* these always succeed */
    gu_mutex_init (&queue->lock,  NULL);
    gu_cond_init  (&queue->ready, NULL);

    return queue;
}

int gcs_queue_push (gcs_queue_t *queue, void *data)
{
    gcs_queue_memb_t *memb = GU_MALLOC (gcs_queue_memb_t);
    int ret;

    if (NULL == memb) return -ENOMEM;

    memb->data = data;
    memb->next = NULL;

    if (!(ret = gu_mutex_lock (&queue->lock))) {
        if (!(ret = queue->err))
        {
            if (queue->tail)
                queue->tail->next = memb;
            else /* empty queue */
                queue->head = memb;

            if (!queue->next)
                queue->next = memb;

            queue->tail = memb;
            ret = ++queue->length;
//            gu_debug ("queue %p: tail: %p data: %p\n",
//                      queue, queue->tail, queue->tail->data);
            /* signal to whoever could be waiting for queue */
            gu_cond_signal  (&queue->ready);
            gu_mutex_unlock (&queue->lock);
        }
    }

    return ret;
}

int gcs_queue_pop (gcs_queue_t *queue, void **data)
{
    gcs_queue_memb_t *head = NULL;
    int ret;

    *data = NULL;
    if (!(ret = gu_mutex_lock (&queue->lock)))
    {
	if (!(ret = queue->err) && (head = queue->head))
	{
	    *data = head->data;
	    queue->head = head->next;
	    if (queue->next == head) queue->next = head->next;
	    if (queue->head == NULL)
	    {
		queue->tail = NULL; /* last member */
	    }
	    ret = --queue->length;
            gu_free (head);
	}
        gu_mutex_unlock (&queue->lock);
    }

    return ret;
}

void gcs_queue_cleanup (void *p) { gu_mutex_unlock (p); }

int gcs_queue_pop_wait (gcs_queue_t *queue, void **data)
{
    int ret;
    gcs_queue_memb_t *head = NULL;
    
    *data = NULL;

    /* at this point it can happen that queue->err is raised and
     * lock destroyed! what to do? */
    if (!(ret = gu_mutex_lock (&queue->lock)))
    {
	/* installing cleanup every time when thread goes to wait
	 * seems to be expensive.
	 * However we're going to wait anyways, ne, Akane-chan? */
	while (NULL == queue->head && !(ret = queue->err))
	{
	    pthread_cleanup_push (gcs_queue_cleanup, &queue->lock);
	    gu_cond_wait (&queue->ready, &queue->lock);
	    pthread_cleanup_pop (0);
	}

	if (queue->head)
	{
	    head        = queue->head;
	    queue->head = head->next;
	    *data       = head->data;
	    if (queue->next == head) queue->next = queue->head;
	    if (queue->head == NULL)
	    {
		queue->tail = NULL; /* last member */
	    }
	    ret = --queue->length;
            assert (queue->length >= 0);
            gu_free (head);
	}
        else {
            assert (ret == queue->err);
        }

        gu_mutex_unlock (&queue->lock);
    }

#ifdef GCS_DEBUG_QUEUE
    if (ret < 0) gu_debug ("Returning %d (%s)", ret, strerror(-ret));
#endif
    return ret;
}

int gcs_queue_next (gcs_queue_t *queue, void **data)
{
    *data = NULL;
    if (!gu_mutex_lock (&queue->lock))
    {
        if (!queue->err && NULL != queue->next)
	{
	    *data = queue->next->data;
	    queue->next = queue->next->next;
	}
        gu_mutex_unlock (&queue->lock);
    }

    return queue->err;
}

int gcs_queue_next_wait (gcs_queue_t *queue, void **data)
{
    *data = NULL;
    if (!gu_mutex_lock (&queue->lock))
    {
	/* installing cleanup every time when thread goes to wait
	 * seems to be expensive. However we're going to wait anyways */
	while ((NULL == queue->next) && (!queue->err))
	{
//	    printf ("queue %p: next is %p\n", queue, queue->next);
	    pthread_cleanup_push (gcs_queue_cleanup, &queue->lock);
	    gu_cond_wait (&queue->ready, &queue->lock);
	    pthread_cleanup_pop (0);
//	    printf ("queue %p: received signal, next is %p\n",
//		    queue, queue->next);
	}

//	printf ("queue %p: next: %p data: %p\n",
//		queue, queue->next, queue->next->data);
	if (queue->next)
	{
	    *data = queue->next->data;
	    queue->next = queue->next->next;
	}
//	printf ("queue %p: next next: %p\n",
//		queue, queue->next); //fflush (stdout);
	
        gu_mutex_unlock (&queue->lock);
        return queue->err;
    }
    else return -ENODATA;

}

/* this one is not fool proof */
int gcs_queue_abort (gcs_queue_t *queue)
{
    gcs_queue_memb_t *m = NULL;

    /* This could deadlock if some thread waiting for "ready" condition
     * was cancelled. Is there any better solution that installing cleanup
     * handlers every time in gcs_queue_*_wait functions ? */
    if (!gu_mutex_lock (&queue->lock) && !queue->err)
    {
	queue->err = -ECONNABORTED;
	gu_mutex_unlock (&queue->lock);

	/* at this point no items will be appended to queue and
	 * no more threads will go waiting for condition */

	/* Signal everybody who's been waiting for signal 
	 * to go and complete their deeds */
        gu_cond_broadcast (&queue->ready);
	
	/* at this point there will be no more threads waiting for signal */
	/* if there still are some items in the queue, free them manually */
	while ((m = queue->head))
	{
	    queue->head = m->next;
	    gu_free (m->data);
	    gu_free (m);
	}
	queue->next = NULL;
	
        queue->length = 0;
	return 0;
    }
    else
    {
	gu_mutex_unlock (&queue->lock);
	return queue->err;
    }
}

int gcs_queue_reset (gcs_queue_t *queue)
{
    long ret;

    ret = gu_mutex_lock (&queue->lock);
    if (!ret)
    {
	queue->err = 0;
	gu_mutex_unlock (&queue->lock);
    }
    return ret;
}

int gcs_queue_free (gcs_queue_t *queue)
{
    gcs_queue_memb_t *m = NULL;

    /* This could deadlock if some thread waiting for "ready" condition
     * was cancelled. Is there any better solution that installing cleanup
     * handlers every time in gcs_queue_*_wait functions ? */
    if (!gu_mutex_lock (&queue->lock) && (queue->err != -ENODATA))
    {
	queue->err = -ENODATA;
	gu_mutex_unlock (&queue->lock);

	/* at this point no items will be appended to queue and
	 * no more threads will go waiting for condition */

	/* Signal everybody who's been waiting for signal 
	 * to go and complete their deeds */
	while (gu_cond_destroy (&queue->ready))
	    gu_cond_signal (&queue->ready);
	
	/* at this point there will be no more threads waiting for signal */
	/* if there still are some items in the queue, free them manually */

	while ((m = queue->head))
	{
	    queue->head = m->next;
	    gu_free (m->data);
	    gu_free (m);
	}
	queue->next = NULL;
	
	/* We can't go around this */
        // it seems like not all threads that were woken up manage to
        // grab a mutex at this point
	while (gu_mutex_destroy (&queue->lock))
	{  /* wait till the mutex is freed by other threads */
	    //gu_debug ("Waiting for other threads to release the mutex");
            usleep (50000);
	    gu_mutex_lock (&queue->lock);
	    gu_mutex_unlock (&queue->lock);
	}
        //gu_debug ("Mutex destoryed successfully");

	/* Now we can safely free allocated memory */
	gu_free (queue);
        //gu_debug ("Queue freed successfully");
	/* Wow! */
	return 0;
    }
    else
    {
	gu_mutex_unlock (&queue->lock);
	return queue->err;
    }
}
