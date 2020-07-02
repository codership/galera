/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include "gu_macros.h"
#include "gu_threads.h"
#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include <cerrno>
#include <cstring>
#include <cassert>
#include <cstdlib> // abort()

#if !defined(GU_DEBUG_MUTEX) && !defined(NDEBUG)
#define GU_MUTEX_DEBUG
#endif

namespace gu
{
    class Mutex
    {
    public:

        Mutex () : value_()
#ifdef GU_MUTEX_DEBUG
                 , owned_()
                 , locked_()
#endif /* GU_MUTEX_DEBUG */
        {
            gu_mutex_init (&value_, NULL); // always succeeds
        }

        ~Mutex ()
        {
            int const err(gu_mutex_destroy (&value_));
            if (gu_unlikely(err != 0))
            {
                assert(0);
                gu_throw_error(err) << "gu_mutex_destroy()";
            }
        }

        void lock() const
        {
            int const err(gu_mutex_lock(&value_));
            if (gu_likely(0 == err))
            {
#ifdef GU_MUTEX_DEBUG
                locked_ = true;
                owned_  = gu_thread_self();
#endif /* GU_MUTEX_DEBUG */
            }
            else
            {
                assert(0);
                gu_throw_error(err) << "Mutex lock failed";
            }
        }

        void unlock() const
        {
            // this is not atomic, but the presumption is that unlock()
            // should never be called before preceding lock() completes
#if defined(GU_DEBUG_MUTEX) || defined(GU_MUTEX_DEBUG)
            assert(locked());
            assert(owned());
#if defined(GU_MUTEX_DEBUG)
            locked_ = false;
#endif /* GU_MUTEX_DEBUG */
            disown();
#endif /* GU_DEBUG_MUTEX */
            int const err(gu_mutex_unlock(&value_));
            if (gu_unlikely(0 != err))
            {
                log_fatal << "Mutex unlock failed: " << err << " ("
                          << strerror(err) << "), Aborting.";
                ::abort();
            }
        }

        gu_mutex_t& impl() const { return value_; }

#if defined(GU_DEBUG_MUTEX)
        bool locked() const { return gu_mutex_locked(&value_); }
        bool owned()  const { return locked() && gu_mutex_owned(&value_);  }
        void disown() const { gu_mutex_disown(&value); }
#elif defined(GU_MUTEX_DEBUG)
        bool locked() const { return locked_; }
        bool owned()  const { return locked() && gu_thread_equal(owned_,gu_thread_self()); }
        void disown() const
        {
            memset(&owned_, 0, sizeof(owned_));
        }
#endif /* GU_DEBUG_MUTEX */
    protected:

        gu_mutex_t  mutable value_;
#ifdef GU_MUTEX_DEBUG
        gu_thread_t mutable owned_;
        bool        mutable locked_;
#endif /* GU_MUTEX_DEBUG */

    private:

        Mutex (const Mutex&);
        Mutex& operator= (const Mutex&);

        friend class Lock;
    };

    class RecursiveMutex
    {
    public:
        RecursiveMutex() : mutex_()
        {
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&mutex_, &mattr);
            pthread_mutexattr_destroy(&mattr);
        }

        ~RecursiveMutex()
        {
            pthread_mutex_destroy(&mutex_);
        }

        void lock()
        {
            if (pthread_mutex_lock(&mutex_)) gu_throw_fatal;
        }

        void unlock()
        {
            if (pthread_mutex_unlock(&mutex_)) gu_throw_fatal;
        }

    private:
        RecursiveMutex(const RecursiveMutex&);
        void operator=(const RecursiveMutex&);

        pthread_mutex_t mutex_;
    };
}

#endif /* __GU_MUTEX__ */
