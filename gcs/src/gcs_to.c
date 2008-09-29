/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! \file \brief Total order access "class" implementation.
 * Although gcs_repl() and gcs_recv() calls return sequence
 * numbers in total order, there are concurrency issues between
 * application threads and they can grab critical section
 * mutex out of order. Wherever total order access to critical
 * section is required, these functions can be used to do this.
 */

#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <galerautils.h>

#include "gcs.h"

#define TO_USE_SIGNAL 1

typedef enum  {
  HOLDER = 0, //!< current TO holder
  WAIT,       //!< actively waiting in the queue
  CANCELED,   //!< Waiter has canceled its to request
  WITHDRAW,   //!< marked to be withdrawn
  RELEASED    //!< has been released, free entry now
} waiter_state_t;

typedef struct
{
#ifdef TO_USE_SIGNAL
    gu_cond_t       cond;
#else
    pthread_mutex_t mtx;  // have to use native pthread for double locking
#endif
    waiter_state_t  state;
    int             has_aborted;
}
to_waiter_t;

struct gcs_to
{
    volatile gcs_seqno_t seqno;
    size_t               used;
    size_t               qlen;
    size_t               qmask;
    to_waiter_t*         queue;
    gu_mutex_t           lock;
};

/** Returns pointer to the waiter with the given seqno */
static inline to_waiter_t*
to_get_waiter (gcs_to_t* to, gcs_seqno_t seqno)
{
    return (to->queue + (seqno & to->qmask));
}

gcs_to_t *gcs_to_create (int len, gcs_seqno_t seqno)
{
    gcs_to_t *ret;

    assert (seqno >= 0);

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
#ifdef TO_USE_SIGNAL
		gu_cond_init (&w->cond, NULL);
#else
                pthread_mutex_init (&w->mtx, NULL);
#endif
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
#ifdef TO_USE_SIGNAL
	if (gu_cond_destroy (&w->cond)) {
            // @todo: what if someone is waiting?
	    gu_warn ("Failed to destroy condition %d. Should not happen", i);
	}
#else
	if (pthread_mutex_destroy (&w->mtx)) {
            // @todo: what if someone is waiting?
	    gu_warn ("Failed to destroy mutex %d. Should not happen", i);
	}
#endif
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

    assert (seqno >= 0);

    if ((err = gu_mutex_lock(&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }

    if (seqno < to->seqno) {
	gu_mutex_unlock(&to->lock);
	return -ECANCELED;
    }

    // Check for queue overflow. Tell application that it should wait.
    if (seqno >= to->seqno + to->qlen) {
	gu_mutex_unlock(&to->lock);
	return -EAGAIN;
    }        

    w = to_get_waiter (to, seqno);

    switch (w->state) {
    case CANCELED:
	err = -ECANCELED;
	break;
    case RELEASED:
	if (seqno == to->seqno) {
	    w->state = HOLDER;
	} else if (seqno < to->seqno) {
	    gu_error("Trying to grab outdated seqno");
	    err = -ECANCELED;
	} else { /* seqno > to->seqno, wait for my turn */
	    w->state = WAIT;
	    to->used++;
#ifdef TO_USE_SIGNAL
	    gu_cond_wait(&w->cond, &to->lock);
#else
            pthread_mutex_lock (&w->mtx);
            pthread_mutex_unlock (&to->lock);
            pthread_mutex_lock (&w->mtx); // wait for unlock by other thread
            pthread_mutex_lock (&to->lock);
            pthread_mutex_unlock (&w->mtx);
#endif
	    to->used--;
	    if (w->state == WAIT) { // should be most probable
                assert (seqno == to->seqno);
		w->state = HOLDER;
	    } else if (w->state > WAIT)
		err = -ECANCELED;
	    else {
		gu_fatal("Invalid cond wait exit state %d, seqno %llu(%llu)",
                         w->state, seqno, to->seqno);
		abort();
	    }
	}
	break;
    default:
	gu_fatal("TO queue over wrap");
	abort();
    }
    
    gu_mutex_unlock(&to->lock);
    return err;
}

static inline long
to_wake_waiter (to_waiter_t* w)
{
    long err = 0;

    if (w->state == WAIT) {
#ifdef TO_USE_SIGNAL
        err = gu_cond_signal (&w->cond);
#else
        err = pthread_mutex_unlock (&w->mtx);
#endif
        if (err) {
            gu_fatal ("gu_cond_signal failed: %d", err);
        }
    }
    return err;
}

static inline void
to_release_and_wake_next (gcs_to_t* to, to_waiter_t* w) {
    w->state = RELEASED;
    /* Iterate over CANCELED waiters and set states as RELEASED */
    for (to->seqno++;
         (w = to_get_waiter(to, to->seqno)) && w->state == CANCELED;
         to->seqno++) {
        w->state = RELEASED;
    }
    to_wake_waiter (w);
}

int gcs_to_release (gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock(&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }

    w = to_get_waiter (to, seqno);
    
    if (seqno == to->seqno) {
        to_release_and_wake_next (to, w);
    } else if (seqno > to->seqno) {
	if (w->state != CANCELED) {
	    gu_fatal("Illegal state in premature release: %d", w->state);
	    abort();
	}
	/* Leave state CANCELED so that real releaser can iterate  */
    } else {
	/* */
	if (w->state != RELEASED) {
	    gu_fatal("Outdated seqno and state not RELEASED: %d", w->state);
	    abort();
	}
    }

    gu_mutex_unlock(&to->lock);

    return err;
}

gcs_seqno_t gcs_to_seqno (gcs_to_t* to)
{
    return to->seqno - 1;
}

int gcs_to_cancel (gcs_to_t *to, gcs_seqno_t seqno)
{
    int err;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    
    // Check for queue overflow. This is totally unrecoverable. Abort.
    if (seqno >= to->seqno + to->qlen) {
	gu_mutex_unlock(&to->lock);
	abort();
    }        

    w = to_get_waiter (to, seqno);
    if (seqno > to->seqno) {
        err = to_wake_waiter (w);
	w->state = CANCELED;
    } else if (seqno == to->seqno) {
	gu_warn("tried to cancel myself: state %d seqno %llu",
		 w->state, seqno);
        err = -ECANCELED;
    } else {
	gu_warn("trying to cancel used seqno: state %d cancel seqno = %llu, "
		"TO seqno = %llu", w->state, seqno, to->seqno);
        err = -ECANCELED;        
    }
    
    gu_mutex_unlock (&to->lock);
    return err;
}

int gcs_to_self_cancel(gcs_to_t *to, gcs_seqno_t seqno)
{
    int err = 0;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }

    // Check for queue overflow. Tell application that it should wait.
    if (seqno >= to->seqno + to->qlen) {
	gu_mutex_unlock(&to->lock);
	return -EAGAIN;
    }        

    w = to_get_waiter(to, seqno);

    if (seqno > to->seqno) { // most probable case
        w->state = CANCELED;
    }
    else if (seqno == to->seqno) {
        // have to wake the next waiter as if we grabbed and now releasing TO
        to_release_and_wake_next (to, w);
    }
    else { // (seqno < to->seqno)
        // This waiter must have been canceled or even released by preceding
        // waiter. Do nothing.
    }
    
    gu_mutex_unlock(&to->lock);
    
    return err;
}

int gcs_to_withdraw (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    int err;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
    {
        if (seqno >= to->seqno) {
            to_waiter_t *w = to_get_waiter (to, seqno);
            w->state       = WITHDRAW;
            w->has_aborted = 0;
            rcode = to_wake_waiter (w);
        } else {
            gu_warn ("trying to withdraw used seqno: cancel seqno = %llu, "
                     "TO seqno = %llu", seqno, to->seqno);
            /* gu_mutex_unlock (&to->lock); */
            rcode = -ERANGE;
        }
    }
    gu_mutex_unlock (&to->lock);
    return rcode;
}

int gcs_to_renew_wait (gcs_to_t *to, gcs_seqno_t seqno)
{
    int rcode;
    int err;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
	gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
	abort();
    }
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

