/*
 * Copyright (C) 2009-2015 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include "gu_macros.h"
#include "gu_mutex.h"
#include "gu_logger.hpp"
#include "gu_throw.hpp"

#include <pthread.h>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <cstdlib> // abort()

namespace gu
{
    class Mutex
    {
    public:

        Mutex () : value_()
#ifndef NDEBUG
                 , locked_()
#endif /* NDEBUG*/
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
#ifndef NDEBUG
                locked_ = true;
                owned_  = pthread_self();
#endif /* NDEBUG */
            }
            else
            {
                gu_throw_error(err) << "Mutex lock failed: ";
            }
        }

        void unlock() const
        {
#ifndef NDEBUG
            // this is not atomic, but the presumption is that unlock()
            // should never be called before preceding lock() completes
            assert(locked_);
            locked_ = false;
#endif /* NDEBUG */
            int const err(gu_mutex_unlock(&value_));
            if (gu_unlikely(0 != err))
            {
                log_fatal << "Mutex unlock failed: " << err << " ("
                          << strerror(err) << "), Aborting.";
                ::abort();
            }
        }

#ifndef NDEBUG
        bool locked() const { return locked_; }
        bool owned() const { return (owned_ == pthread_self()); }
#endif /* NDEBUG */

    protected:

        gu_mutex_t mutable value_;
#ifndef NDEBUG
        bool       mutable locked_;
        pthread_t  mutable owned_;
#endif /* NDEBUG */

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
