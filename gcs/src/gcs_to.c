// Copyright (C) 2007 Codership Oy <info@codership.com>

/*! \file \brief Total order access "class" implementation.
 * Although gcs_repl() and gcs_recv() calls return sequence
 * numbers in total order, there are concurrency issues between
 * application threads and they can grab critical section
 * mutex out of order. Wherever total order access to critical
 * section is required, these functions can be used to do this.
 */

#include <pthread.h>
#include <errno.h>

#include <galerautils.h>

#include "gcs.h"

typedef enum  {
  HOLDER = 0, //!< current TO holder
  WAIT,       //!< actively waiting in the queue
  MUST_ABORT, //!< marked as aborting
  WITHDRAW,   //!< marked to be withdrawn
  RELEASED    //!< has been released, free entry now
} waiter_state_t;

typedef struct
{
    gu_cond_t cond;
    waiter_state_t state;
    int       has_aborted;
}
to_waiter_t;

struct gcs_to
{
    volatile gcs_seqno_t seqno;
    size_t               used;
    size_t               qlen;
    size_t               qmask;
    //gu_cond_t  *queue;
    to_waiter_t*         queue;
    gu_mutex_t           lock;
};

/** Returns pointer to the waiter with the given seqno */
static inline to_waiter_t*
to_get_waiter (gcs_to_t* to, gcs_seqno_t seqno)
{
    return (to->queue + (seqno & to->qmask));
}

#ifdef REMOVED
static set_bit(struct gcs_to *to, gcs_seqno_t seqno) {
  to->aborted[seqno / sizeof(char)] |= (1 << (seqno % sizeof(char));
}
#endif

gcs_to_t *gcs_to_create (int len, gcs_seqno_t seqno)
{
    gcs_to_t *ret;

    if (len <= 0) {
	gu_error ("Negative length parameter: %d", len);
	return NULL;
    }

    ret = GU_CALLOC (1, gcs_to_t);
    
    if (ret) {

	/* Make queue length a power of 2 */
	ret->qlen = 1;
	while (ret->qlen < len) {
	    // unsigned, can be bigger than any integer
	    ret->qlen = ret->qlen << 1;
	}
	ret->qmask = ret->qlen - 1;
	ret->seqno = seqno;

	ret->queue = GU_CALLOC (ret->qlen, to_waiter_t);

	if (ret->queue) {
	    size_t i;
	    for (i = 0; i < ret->qlen; i++) {
                to_waiter_t *w = ret->queue + i;
		gu_cond_init (&w->cond, NULL);
                w->state       = RELEASED;
                w->has_aborted = 0;
	    }
	    gu_mutex_init (&ret->lock, NULL);
	
	    return ret;
	}

	gu_free (ret);
    }

    return NULL;
}

int gcs_to_destroy (gcs_to_t** to)
{
    gcs_to_t *t = *to;
    int ret;
    size_t i;

    gu_mutex_lock (&t->lock);
    if (t->used) {
	gu_mutex_unlock (&t->lock);
	return -EBUSY;
    }
    
    for (i = 0; i < t->qlen; i++) {
        to_waiter_t *w = t->queue + i;
	if (gu_cond_destroy (&w->cond)) {
            // @todo: what if someone is waiting?
	    gu_warn ("Failed to destroy condition %d. Should not happen", i);
	}
    }    
    t->qlen = 0;
    
    gu_mutex_unlock (&t->lock);
    /* What else can be done here? */
    ret = gu_mutex_destroy (&t->lock);
    if (ret) return -ret; // application can retry

    gu_free (t->queue);
    gu_free (t);
    *to = NULL;
    return 0;
}

int gcs_to_grab (gcs_to_t* to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;

    if ((err = gu_mutex_lock(&to->lock))) return -err;

    w = to_get_waiter (to, seqno);

    switch (w->state) {
    case MUST_ABORT:
        if (to->seqno == seqno) {
            gu_mutex_unlock (&to->lock);
            gcs_to_release(to, seqno);
        } else {
            w->has_aborted = 1;
            gu_mutex_unlock(&to->lock);
        }
        return -ECANCELED;
    case WITHDRAW:
        w->state = RELEASED;
        gu_mutex_unlock(&to->lock);
        return  -ECANCELED;
    case WAIT:
    case HOLDER:
        gu_error ("TO queue over wrap at seqno = %llu ", seqno);
	gu_mutex_unlock (&to->lock);
	return -ERANGE;	// bug in application
    case RELEASED:
        w->state = WAIT;
        break;
    }
    
    if (to->seqno == seqno) {
      w->state = HOLDER;
      gu_mutex_unlock(&to->lock);
      return 0; 
    }

    if (seqno > to->seqno) {
	if (to->qlen > seqno - to->seqno) {

	    to->used++; // mark TO that it is used by a waiting thread
	    gu_cond_wait (&w->cond, &to->lock);
	    to->used--;

            switch (w->state) {
            case MUST_ABORT:
                if (to->seqno == seqno) {
                    gu_mutex_unlock (&to->lock);
                    gcs_to_release(to, seqno);
                } else {
                  w->has_aborted = 1;
                  gu_mutex_unlock (&to->lock);
                }
                return -ECANCELED;
            case WITHDRAW:
                w->state = RELEASED;
                gu_mutex_unlock (&to->lock);
                return -ECANCELED;
            case WAIT:
                w->state = HOLDER;
                break;
            case RELEASED:
            case HOLDER:
                gu_error ("TO bad waiter state %d at seqno = %llu ", 
                          w->state, seqno);
                gu_mutex_unlock (&to->lock);
                return -ERANGE;	// bug in application
                break;
            }

	    if (to->seqno != seqno) { // Very, very bad. Perhaps fatal.
		gu_mutex_unlock (&to->lock);
		return -ENOTRECOVERABLE;
	    }

            gu_mutex_unlock(&to->lock);
	    return 0; // return with locked mutex
	}
	else {
	    gu_mutex_unlock (&to->lock);
	    return -EAGAIN; // no space in queue
	}
    }
    else {
        gu_error ("trying to grab outdated seqno: my seqno = %llu, "
			   "TO seqno = %llu", seqno, to->seqno);
	gu_mutex_unlock (&to->lock);
	return -ERANGE;	// outdated seqno, bug in application
    }
}

int gcs_to_release (gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;

    if ((err = gu_mutex_lock(&to->lock))) return -err;
    {
        to_waiter_t *w = to_get_waiter (to, seqno);

        if (to->seqno == seqno) {
            int found_aborted;

            w->state       = RELEASED;
            w->has_aborted = 0;

            do {
                /* queuers, who have already aborted, are not able to release
                 * next waiter. Therefore we must bypass them here.
                 */
                to->seqno++; // Next seqno
                w = to_get_waiter (to, to->seqno);
                gu_cond_signal (&w->cond);
                found_aborted = w->has_aborted;

                if (w->has_aborted) {
                    w->state       = RELEASED;
                    w->has_aborted = 0;
                }
            } while (found_aborted);
        }
        else {
            gu_warn ("trying to release outdated seqno: my seqno = %llu, "
                     "TO seqno = %llu abort: %d %d",
                     seqno, to->seqno, w->state, w->has_aborted);
            /* Application error*/
            err = -ERANGE;
            w->state = RELEASED;
        }
    }
    gu_mutex_unlock (&to->lock);

    return err;
}

gcs_seqno_t gcs_to_seqno (gcs_to_t* to)
{
    return to->seqno - 1;
}

int gcs_to_cancel (gcs_to_t *to, gcs_seqno_t seqno)
{
    /* we assume, that caller is now holding the TO 
     * However, it is possible that caller wants to cancel a trx
     * which has already left TO. In this case, we avoid the cancelling,
     * there is no need to cancel anymore and besides we don't even
     * have the right to cancel.
     */

    int rcode;
    gu_mutex_lock (&to->lock);
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = MUST_ABORT;
            //w->has_aborted = 0;
            rcode = gu_cond_signal (&w->cond);
        } else {
            gu_warn ("trying to cancel used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

int gcs_to_withdraw (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    gu_mutex_lock (&to->lock);
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = WITHDRAW;
            w->has_aborted = 0;
            rcode = gu_cond_signal (&w->cond);
        } else {
            gu_warn ("trying to withdraw used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            gu_mutex_unlock (&to->lock);
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

int gcs_to_renew_wait (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    gu_mutex_lock (&to->lock);
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = RELEASED;
            w->has_aborted = 0;
            rcode = 0;
        } else {
            gu_warn ("trying to renew used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

