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
#include <stdlib.h> // abort()

#include "gu_log.h"
#include "gu_assert.h"
#include "gu_mem.h"
#include "gu_threads.h"
#include "gu_to.h"

#define TO_USE_SIGNAL 1

typedef enum  {
  HOLDER = 0, //!< current TO holder
  WAIT,       //!< actively waiting in the queue
  CANCELED,   //!< Waiter has canceled its to request
  INTERRUPTED,//!< marked to be interrupted
  RELEASED,   //!< has been released, free entry now
} waiter_state_t;

typedef struct
{
#ifdef TO_USE_SIGNAL
    gu_cond_t       cond;
#else
    pthread_mutex_t mtx;  // have to use native pthread for double locking
#endif
    waiter_state_t  state;
}
to_waiter_t;

struct gu_to
{
    volatile gu_seqno_t seqno;
    size_t               used; /* number of active waiters */
    ssize_t              qlen;
    size_t               qmask;
    to_waiter_t*         queue;
    gu_mutex_t           lock;
};

/** Returns pointer to the waiter with the given seqno */
static inline to_waiter_t*
to_get_waiter (gu_to_t* to, gu_seqno_t seqno)
{
    // Check for queue overflow. Tell application that it should wait.
    if (seqno >= to->seqno + to->qlen) {
        return NULL;
    }        

    return (to->queue + (seqno & to->qmask));
}

gu_to_t *gu_to_create (int len, gu_seqno_t seqno)
{
    gu_to_t *ret;

    assert (seqno >= 0);

    if (len <= 0) {
        gu_error ("Negative length parameter: %d", len);
        return NULL;
    }

    ret = GU_CALLOC (1, gu_to_t);
    
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
            ssize_t i;
            for (i = 0; i < ret->qlen; i++) {
                to_waiter_t *w = ret->queue + i;
#ifdef TO_USE_SIGNAL
                gu_cond_init (&w->cond, NULL);
#else
                pthread_mutex_init (&w->mtx, NULL);
#endif
                w->state       = RELEASED;
            }
            gu_mutex_init (&ret->lock, NULL);
        
            return ret;
        }
        gu_free (ret);
    }

    return NULL;
}

long gu_to_destroy (gu_to_t** to)
{
    gu_to_t *t = *to;
    long      ret;
    ssize_t    i;

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

long gu_to_grab (gu_to_t* to, gu_seqno_t seqno)
{
    long err;
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

    if ((w = to_get_waiter (to, seqno)) == NULL) {
        gu_mutex_unlock(&to->lock);
        return -EAGAIN;
    }        
    /* we have a valid waiter now */

    switch (w->state) {
    case INTERRUPTED:
        w->state = RELEASED;
        err = -EINTR;
        break;
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
            switch (w->state) { 
            case WAIT:// should be most probable
                assert (seqno == to->seqno);
                w->state = HOLDER;
                break;
            case INTERRUPTED:
                w->state = RELEASED;
                err      = -EINTR;
                break;
            case CANCELED:
                err = -ECANCELED;
                break;
            case RELEASED:
                /* this waiter has been cancelled */
                assert(seqno < to->seqno);
                err = -ECANCELED;
                break;
            default:
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
to_release_and_wake_next (gu_to_t* to, to_waiter_t* w) {
    w->state = RELEASED;
    /* 
     * Iterate over CANCELED waiters and set states as RELEASED
     * We look for waiter in the head of queue, which guarantees that
     * to_get_waiter() will always return a valid waiter pointer
     */
    for (to->seqno++;
         (w = to_get_waiter(to, to->seqno)) && w && w->state == CANCELED;
         to->seqno++) {
        w->state = RELEASED;
    }
    to_wake_waiter (w);
}

long gu_to_release (gu_to_t *to, gu_seqno_t seqno)
{
    long         err;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock(&to->lock))) {
        gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
        abort();
    }

    if ((w = to_get_waiter (to, seqno)) == NULL) {
        gu_mutex_unlock(&to->lock);
        return -EAGAIN;
    }        
    /* we have a valid waiter now */
    
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

gu_seqno_t gu_to_seqno (gu_to_t* to)
{
    return to->seqno - 1;
}

long gu_to_cancel (gu_to_t *to, gu_seqno_t seqno)
{
    long         err;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
        gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
        abort();
    }
    
    // Check for queue overflow. This is totally unrecoverable. Abort.
    if ((w = to_get_waiter (to, seqno)) == NULL) {
        gu_mutex_unlock(&to->lock);
        abort();
    }        
    /* we have a valid waiter now */

    if ((seqno > to->seqno) || 
        (seqno == to->seqno && w->state != HOLDER)) {
        err = to_wake_waiter (w);
        w->state = CANCELED;
    } else if (seqno == to->seqno && w->state == HOLDER) {
        gu_warn("tried to cancel current TO holder, state %d seqno %llu",
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

long gu_to_self_cancel(gu_to_t *to, gu_seqno_t seqno)
{
    long         err = 0;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
        gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
        abort();
    }

    if ((w = to_get_waiter (to, seqno)) == NULL) {
        gu_mutex_unlock(&to->lock);
        return -EAGAIN;
    }        
    /* we have a valid waiter now */

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

long gu_to_interrupt (gu_to_t *to, gu_seqno_t seqno)
{
    long rcode = 0;
    long err;
    to_waiter_t *w;

    assert (seqno >= 0);

    if ((err = gu_mutex_lock (&to->lock))) {
        gu_fatal("Mutex lock failed (%d): %s", err, strerror(err));
        abort();
    }
    if (seqno >= to->seqno) {
        if ((w = to_get_waiter (to, seqno)) == NULL) {
            gu_mutex_unlock(&to->lock);
            return -EAGAIN;
        }        
        /* we have a valid waiter now */

        switch (w->state) {
        case HOLDER:
            gu_debug ("trying to interrupt in use seqno: seqno = %llu, "
                      "TO seqno = %llu", seqno, to->seqno);
            /* gu_mutex_unlock (&to->lock); */
            rcode = -ERANGE;
            break;
        case CANCELED:
            gu_debug ("trying to interrupt canceled seqno: seqno = %llu, "
                      "TO seqno = %llu", seqno, to->seqno);
            /* gu_mutex_unlock (&to->lock); */
            rcode = -ERANGE;
            break;
        case WAIT:
            gu_debug ("signaling to interrupt wait seqno: seqno = %llu, "
                      "TO seqno = %llu", seqno, to->seqno);
            rcode    = to_wake_waiter (w);
            /* fall through */
        case RELEASED:
            w->state = INTERRUPTED;
            break;
        case INTERRUPTED:
            gu_debug ("TO waiter interrupt already seqno: seqno = %llu, "
                      "TO seqno = %llu", seqno, to->seqno);
            break;
        }
    } else {
        gu_debug ("trying to interrupt used seqno: cancel seqno = %llu, "
                  "TO seqno = %llu", seqno, to->seqno);
        /* gu_mutex_unlock (&to->lock); */
        rcode = -ERANGE;
    }

    gu_mutex_unlock (&to->lock);
    return rcode;
}


