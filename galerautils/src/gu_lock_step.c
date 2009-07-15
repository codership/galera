/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/*
 * Universally Unique IDentifier. RFC 4122.
 * Time-based implementation.
 *
 */

#include <stdlib.h> // abort()
#include <errno.h>  // error codes
#include <sys/time.h>
#include <time.h>
#include <string.h> // strerror()

#include "gu_log.h"
#include "gu_assert.h"
#include "gu_time.h"
#include "gu_lock_step.h"

void
gu_lock_step_init (gu_lock_step_t* ls)
{
    gu_mutex_init (&ls->mtx, NULL);
    gu_cond_init  (&ls->cond, NULL);
    ls->wait    = 0;
    ls->cont    = 0;
    ls->enabled = false;
}

void
gu_lock_step_destroy (gu_lock_step_t* ls)
{
    // this is not really fool-proof, but that's not for fools to use
    while (gu_lock_step_cont(ls, 10)) {};
    gu_cond_destroy  (&ls->cond);
    gu_mutex_destroy (&ls->mtx);
    assert (0 == ls->wait);
}

void
gu_lock_step_enable (gu_lock_step_t* ls, bool enable)
{
    if (!gu_mutex_lock (&ls->mtx)) {
        ls->enabled = enable;
        gu_mutex_unlock (&ls->mtx);
    }
    else {
        gu_fatal ("Mutex lock failed");
        assert (0);
        abort();
    }
}

void
gu_lock_step_wait (gu_lock_step_t* ls)
{
    if (!gu_mutex_lock (&ls->mtx)) {
        if (ls->enabled) {
            if (!ls->cont) {                           // wait for signal
                ls->wait++;
                gu_cond_wait (&ls->cond, &ls->mtx);
            }
            else {                                     // signal to signaller
                gu_cond_signal (&ls->cond);
                ls->cont--;
            }
        }
        gu_mutex_unlock (&ls->mtx);
    }
    else {
        gu_fatal ("Mutex lock failed");
        assert (0);
        abort();
    }
}

/* returns how many waiters are there */
long
gu_lock_step_cont (gu_lock_step_t* ls, long timeout_ms)
{
    long ret = -1;

    if (!gu_mutex_lock (&ls->mtx)) {
        if (ls->enabled) {

            if (ls->wait > 0) {                   // somebody's waiting
                ret = ls->wait;
                gu_cond_signal (&ls->cond);
                ls->wait--;
            }
            else if (timeout_ms > 0) {            // wait for waiter
                // what a royal mess with times! Why timeval exists?
                struct timeval  now;
                struct timespec timeout;
                long err;

                gettimeofday   (&now, NULL);
                gu_timeval_add (&now, timeout_ms * 0.001);
                timeout.tv_sec  = now.tv_sec;
                timeout.tv_nsec = now.tv_usec * 1000;

                ls->cont++;
                do {
                    err = gu_cond_timedwait (&ls->cond, &ls->mtx, &timeout);
                } while (EINTR == err);

                assert ((0 == err) || (ETIMEDOUT == err && ls->cont > 0));

                ret       = (0 == err); // successful rendezvous with waiter
                ls->cont -= (0 != err); // self-decrement in case of error
            }
            else if (timeout_ms < 0) {         // wait forever
                long err;

                ls->cont++;
                err = gu_cond_wait (&ls->cond, &ls->mtx);
                ret       = (0 == err); // successful rendezvous with waiter
                ls->cont -= (0 != err); // self-decrement in case of error
            }
            else {
                // don't wait
                ret = 0;
            }
        }
        gu_mutex_unlock (&ls->mtx);
    }
    else {
        gu_fatal ("Mutex lock failed");
        assert (0);
        abort();
    }

    return ret;
}

