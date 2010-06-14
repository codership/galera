// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * Debug versions of thread functions
 *
 * $Id$
 */
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "galerautils.h"

/* Is it usable? */
static const struct gu_mutex
gu_mutex_init = { .target_mutex      = PTHREAD_MUTEX_INITIALIZER,
                  .control_mutex     = PTHREAD_MUTEX_INITIALIZER,
                  .lock_waiter_count = 0,
                  .cond_waiter_count = 0,
                  .holder_count      = 0,
                  .thread            = 0, // unknown thread
                  .file              = __FILE__,
                  .line              = __LINE__
};

int gu_mutex_init_dbg (struct gu_mutex *m,
                       const pthread_mutexattr_t* attr,
                       const char *file, unsigned int line)
{
    m->file              = file;
    m->line              = line;
    m->lock_waiter_count = 0;
    m->cond_waiter_count = 0;
    m->holder_count      = 0;
    m->thread            = pthread_self();

    pthread_mutex_init(&m->control_mutex, NULL);
    pthread_mutex_init(&m->target_mutex, attr);

    return 0; // as per pthread spec
}

int gu_mutex_lock_dbg(struct gu_mutex *m,
                      const char *file, unsigned int line)
{
    int err = 0;

    pthread_mutex_lock(&m->control_mutex);
    {
        if (m->holder_count > 0 && pthread_equal(pthread_self(), m->thread)) {
            // Have to explicitly submit file and line info as they come
            // from a totally different place
            gu_fatal("Second mutex lock attempt by the same thread, %lu, "
                     "at %s:%d, first locked at %s:%d",
                     pthread_self(), file, line, m->file, m->line);
            assert(0);
            err = EDEADLK; /* return error in case assert is not defined */
        }
        m->lock_waiter_count++;
    }
    /* unlocking control mutex here since we may block waiting for target
     * mutext and unlocking target mutex again involves locking the control */
    pthread_mutex_unlock(&m->control_mutex);
    if (err) return err;

    /* request the actual mutex */
    if ((err = pthread_mutex_lock(&m->target_mutex))) {
        /* This i a valid situation - mutex could be destroyed */
        gu_debug("%lu mutex lock error (%d: %s) at %s:%d",
                  pthread_self(), err, strerror(err), file, line);
        return err;
    }

    /* need control mutex for info field changes */
    if ((err = pthread_mutex_lock(&m->control_mutex))) {
        // do we need this check - it's only a control mutex?
        gu_fatal("%lu mutex lock error (%d: %s) at %s:%d",
                  pthread_self(), err, strerror(err), file, line);
        assert(0);
    }
    else {
        if (gu_likely(m->holder_count == 0)) {
            m->thread = pthread_self();
            m->lock_waiter_count--;
            m->holder_count++;
            m->file = file;
            m->line = line;
        }
        else {
            gu_fatal("Mutex lock granted %d times at %s:%d",
                     m->holder_count, file, line);
            assert(0);
        }
        pthread_mutex_unlock(&m->control_mutex);
    }

    /* we have to return 0 here since target mutex was successfully locked */
    return 0;
}

int gu_mutex_unlock_dbg (struct gu_mutex *m,
                         const char *file, unsigned int line)
{
    int err = 0;

    pthread_mutex_lock(&m->control_mutex);
    {
        /** must take into account that mutex unlocking can happen in
         *  cleanup handlers when thread is terminated in cond_wait().
         *  Then holder_count would still be 0 (see gu_cond_wait()),
         *  but cond_waiter - not */
        if (m->holder_count == 0 && m->cond_waiter_count == 0) {
            gu_fatal ("%lu attempts to unlock unlocked mutex at %s:%d. "
                      "Last use at %s:%d",
                      pthread_self(), file, line,
            m->file ? m->file : "" , m->line);
            assert(0);
        }

        if (m->holder_count > 0  && !pthread_equal(pthread_self(), m->thread)) {
        /** last time pthread_t was unsigned long int */
            gu_fatal ("%lu attempts to unlock mutex owned by %lu at %s:%d. "
                      "Locked at %s:%d",
                      pthread_self(), m->thread,
                      file, line, m->file, m->line);
            assert(0);
            return EPERM; /** return in case assert is undefined */
        }

        err = pthread_mutex_unlock (&m->target_mutex);
        if (gu_likely(!err)) {
            m->file   = file;
            m->line   = line;
            m->thread = 0;
            /* At this point it is difficult to say if we're unlocking
             * normally or from cancellation handler, if holder_count not 0 -
             * assume it is normal unlock, otherwise we decrement
             * cond_waiter_count */
            if (gu_likely(m->holder_count)) {
                m->holder_count--;
            }
            else {
                if (gu_likely(0 != m->cond_waiter_count)) {
                    m->cond_waiter_count--;
                } else {
                    gu_fatal ("Internal galerautils error: both holder_count "
                              "and cond_waiter_count are 0");
                    assert (0);
                }
            }
        }
        else {
            gu_fatal("Error: (%d,%d) during mutex unlock at %s:%d", 
                     err, errno, file, line);
            assert(0);
        }
    }
    pthread_mutex_unlock(&m->control_mutex);

    return err;
}

int gu_mutex_destroy_dbg (struct gu_mutex *m,
                          const char *file, unsigned int line)
{
    int err=0;

    pthread_mutex_lock(&m->control_mutex);
    {
        if (!m->file) {
            gu_fatal("%lu attempts to destroy uninitialized mutex at %s:%d",
                     pthread_self(), file, line);
            assert(0);
        }

        if (m->holder_count != 0) {
            if (pthread_self() == m->thread) {
                gu_fatal ("%lu attempts to destroy mutex locked by "
                          "itself at %s:%d",
                           pthread_self(), m->file, m->line);
                assert (0); /* logical error in program */
            }
            else {
                gu_debug("%lu attempts to destroy a mutex at %s:%d "
                         "locked by %lu at %s:%d (not error)",
                         pthread_self(),
                         file, line, m->thread, m->file, m->line);
//                         assert (0); // DELETE when not needed!
            }
        }

        if (m->cond_waiter_count != 0) {
            gu_debug("%lu attempts to destroy a mutex at %s:%d "
                     "that is waited by %d thread(s)",
                     pthread_self(),
                     file, line, m->cond_waiter_count);
            assert (m->cond_waiter_count > 0);
        }

        if ((err = pthread_mutex_destroy(&m->target_mutex))) {
            gu_debug("Error (%d: %s, %d) during mutex destroy at %s:%d",
                     err, strerror(err), errno, file, line);
            pthread_mutex_unlock (&m->control_mutex);
            return err;
        }

        m->file   = 0;
        m->line   = 0;
        m->thread = 0;

    }
    pthread_mutex_unlock(&m->control_mutex);
    while (pthread_mutex_destroy(&m->control_mutex));

    return err;
}

int gu_cond_wait_dbg (pthread_cond_t *cond, struct gu_mutex *m, 
                      const char *file, unsigned int line)
{
    int err = 0;

    // Unfortunately count updates here are not atomic with cond_wait.
    // But cond_wait() semantics does not allow them to be.

    pthread_mutex_lock (&m->control_mutex);
    {
        if (gu_unlikely(m->holder_count <= 0)) {
            gu_fatal ("%lu tries to wait for condition on unlocked mutex "
                      "at %s %d",
                      pthread_self(), file, line);
            assert (0);
        }
        else if (!pthread_equal(pthread_self(), m->thread)) {
            gu_fatal ("%lu tries to wait for condition on the mutex that"
                      "belongs to %lu at %s %d",
                      pthread_self(), m->thread, file, line);
            assert (0);
        }
        /** pthread_cond_wait frees the mutex */
        m->holder_count--;
        m->cond_waiter_count++;
        m->thread = 0;
        assert (m->holder_count >= 0);
    }
    pthread_mutex_unlock(&m->control_mutex);

    if ((err = pthread_cond_wait (cond, &m->target_mutex))) {
        gu_fatal("Error (%d: %s, %d) during cond_wait at %s:%d",
                 err, strerror(err), errno, file, line);
        assert(0);
    }

    pthread_mutex_lock (&m->control_mutex);
    {
        /** acquired mutex again */
        m->holder_count++;
        m->cond_waiter_count--;
        m->thread = pthread_self();
    }
    pthread_mutex_unlock(&m->control_mutex);

    return err;
}
