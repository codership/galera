// Copyright (C) 2017 Codership Oy <info@codership.com>

/**
 * Debug versions of thread functions
 */

#include "gu_threads.h"

#include "gu_macros.h"
#include "gu_log.h"

#include <assert.h>

#include <errno.h>
#include <string.h> // strerror()

#ifdef GU_DEBUG_MUTEX

int gu_mutex_init_DBG (gu_mutex_t_DBG *m,
                       const gu_mutexattr_t_SYS* attr,
                       const char *file, unsigned int line)
{
    gu_mutex_init_SYS(&m->mutex, attr);
    gu_cond_init_SYS(&m->cond, NULL);

    m->thread            = gu_thread_self_SYS();
    m->file              = file;
    m->line              = line;
    m->waiter_count      = 0;
    m->cond_waiter_count = 0;
    m->locked            = false;

    return 0; // as per pthread spec
}

static inline
void _wait_unlocked(gu_mutex_t_DBG* m)
{
    m->waiter_count++;
    gu_cond_wait_SYS(&m->cond, &m->mutex);
    assert(m->waiter_count > 0);
    m->waiter_count--;
}

int gu_mutex_lock_DBG(gu_mutex_t_DBG *m,
                      const char *file, unsigned int line)
{
    gu_thread_t_SYS const self = gu_thread_self_SYS();

    int const err = gu_mutex_lock_SYS(&m->mutex);

    if (gu_likely(0 == err))
    {
        while (m->locked)
        {
            if (gu_thread_equal_SYS(self, m->thread)) {
                gu_fatal("Second mutex lock attempt by the same thread, %lx, "
                         "at %s:%d, first locked at %s:%d",
                         self, file, line, m->file, m->line);
                abort();
            }

            _wait_unlocked(m);
        }

        m->locked = true;
        m->thread = self;
        m->file = file;
        m->line = line;

        gu_mutex_unlock_SYS(&m->mutex);
    }

    return err;
}

int gu_mutex_unlock_DBG (gu_mutex_t_DBG *m,
                         const char *file, unsigned int line)
{
    gu_thread_t_SYS const self = gu_thread_self_SYS();

    int err = gu_mutex_lock_SYS(&m->mutex);

    if (gu_likely(0 == err))
    {
        if (m->locked && !gu_thread_equal_SYS(self, m->thread)) {
            /** last time pthread_t was unsigned long int */
            gu_fatal ("%lx attempts to unlock mutex at %s:%d "
                      "locked by %lx at %s:%d",
                      self, file, line,
                      m->thread, m->file, m->line);
            assert(0);
            err = EPERM; /** return in case assert is undefined */
        }
        /** must take into account that mutex unlocking can happen in
         *  cleanup handlers when thread is terminated in cond_wait().
         *  Then holder_count would still be 0 (see gu_cond_wait()),
         *  but cond_waiter - not */
        else if (!m->locked && m->cond_waiter_count == 0) {
            gu_error ("%lx attempts to unlock unlocked mutex at %s:%d. "
                      "Last use at %s:%d",
                      self, file, line, m->file ? m->file : "" , m->line);
            assert(0 == m->waiter_count);
            assert(0);
        }
        else
        {
            m->file  = file;
            m->line  = line;
            m->locked = false;

            if (m->waiter_count > 0) gu_cond_signal_SYS(&m->cond);
        }

        gu_mutex_unlock_SYS(&m->mutex);
    }

    return err;
}

int gu_mutex_destroy_DBG (gu_mutex_t_DBG *m,
                          const char *file, unsigned int line)
{
    gu_thread_t_SYS const self = gu_thread_self_SYS();

    int err = gu_mutex_lock_SYS(&m->mutex);

    if (gu_likely(0 == err))
    {
        if (!m->file) {
            gu_fatal("%lx attempts to destroy uninitialized mutex at %s:%d",
                     self, file, line);
            assert(0);
        }
        else if (m->locked) {
            if (gu_thread_equal_SYS(self, m->thread)) {
                gu_error ("%lx attempts to destroy mutex locked by "
                          "itself at %s:%d",
                          self, m->file, m->line);
            }
            else {
                gu_error ("%lx attempts to destroy a mutex at %s:%d "
                          "locked by %lu at %s:%d (not error)",
                          self, file, line, m->thread, m->file, m->line);
            }

            assert (0); /* logical error in program */
            err = EBUSY;
        }
        else if (m->cond_waiter_count != 0) {
            gu_error ("%lx attempts to destroy a mutex at %s:%d "
                      "that is waited by %d thread(s)",
                      self, file, line, m->cond_waiter_count);
            assert (m->cond_waiter_count > 0);
            abort();
        }
        else {
            assert(!m->locked);
            assert(0 == m->cond_waiter_count);

            gu_mutex_unlock_SYS(&m->mutex);

            if ((err = gu_mutex_destroy_SYS(&m->mutex))) {
                gu_debug("Error (%d: %s, %d) during mutex destroy at %s:%d",
                         err, strerror(err), errno, file, line);
            }
            else
            {
                gu_cond_destroy_SYS(&m->cond);

                m->file   = NULL;
                m->line   = 0;
                m->thread = GU_THREAD_INITIALIZER_SYS;
            }

            return err;
        }

        gu_mutex_unlock_SYS(&m->mutex);
    }

    return err;
}

int gu_cond_twait_DBG (gu_cond_t_SYS *cond, gu_mutex_t_DBG *m,
                       const struct timespec *abstime,
                       const char *file, unsigned int line)
{
    gu_thread_t_SYS const self = gu_thread_self_SYS();

    int err = gu_mutex_lock_SYS(&m->mutex);

    if (gu_likely(!err))
    {
        if (gu_unlikely(!m->locked && 0 == m->cond_waiter_count)) {
            gu_fatal ("%lx tries to wait for condition on unlocked mutex "
                      "at %s %d", self, file, line);
            assert (0);
        }
        else if (!gu_thread_equal_SYS(self, m->thread)) {
            gu_fatal ("%lx tries to wait for condition on the mutex locked "
                      "by %lx at %s %d", self, m->thread, file, line);
            assert (0);
        }

        /** gu_cond_wait_SYS frees the mutex */
        m->locked = false;
        m->thread = self;
        m->file = file;
        m->line = line;

        if (m->waiter_count > 0) gu_cond_signal_SYS(&m->cond);

        m->cond_waiter_count++;
        if (NULL == abstime)
            err = gu_cond_wait_SYS (cond, &m->mutex);
        else
            err = gu_cond_timedwait_SYS (cond, &m->mutex, abstime);
        assert(m->cond_waiter_count > 0);
        m->cond_waiter_count--;

        /* now wait till the the mutex is "unlocked" */
        while (m->locked && 0 == err)
        {
            _wait_unlocked(m);
        }

        m->locked = true;
        assert(!gu_thread_equal_SYS(self, m->thread) || 0 != err);
        m->thread = self;
        m->file = file;
        m->line = line;

        gu_mutex_unlock_SYS(&m->mutex);
    }

    return err;
}

#endif /* GU_DEBUG_MUTEX */

#if defined(__APPLE__)

int gu_barrier_init_SYS (gu_barrier_t_SYS *barrier,
                         const gu_barrierattr_t_SYS *attr, unsigned int count)
{
    if(count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if(gu_mutex_init_SYS (&barrier->mutex, 0) < 0)
    {
        return -1;
    }
    if(gu_cond_init_SYS (&barrier->cond, 0) < 0)
    {
        gu_mutex_destroy_SYS (&barrier->mutex);
        return -1;
    }
    barrier->tripCount = count;
    barrier->count = 0;

    return 0;
}

int gu_barrier_destroy_SYS (gu_barrier_t_SYS *barrier)
{
    gu_cond_destroy_SYS  (&barrier->cond);
    gu_mutex_destroy_SYS (&barrier->mutex);
    return 0;
}

int gu_barrier_wait_SYS (gu_barrier_t_SYS *barrier)
{
    gu_mutex_lock_SYS (&barrier->mutex);
    ++(barrier->count);
    if(barrier->count >= barrier->tripCount)
    {
        barrier->count = 0;
        gu_cond_broadcast_SYS (&barrier->cond);
        gu_mutex_unlock_SYS (&barrier->mutex);
        return GU_BARRIER_THREAD_SYS;
    }
    else
    {
        gu_cond_wait_SYS (&barrier->cond, &(barrier->mutex));
        gu_mutex_unlock_SYS (&barrier->mutex);
        return !GU_BARRIER_THREAD_SYS;
    }
}

#endif /* __APPLE__ */
