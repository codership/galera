/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include "gu_macros.h"
#include "gu_threads.h"
#include "gu_throw.hpp"

#include <cerrno>
#include <cstring>

namespace gu
{
    class Mutex
    {
    public:

        Mutex () : value()
        {
            gu_mutex_init (&value, NULL); // always succeeds
        }

        ~Mutex ()
        {
            int err = gu_mutex_destroy (&value);
            if (gu_unlikely(err != 0))
            {
                gu_throw_error (err) << "gu_mutex_destroy()";
            }
        }

        int lock()   const { return gu_mutex_lock(&value); }

        int unlock() const { return gu_mutex_unlock(&value); }

        gu_mutex_t& impl() const { return value; }

#ifdef GU_DEBUG_MUTEX
        bool locked() const { return gu_mutex_locked(&value); }
        bool owned()  const { return gu_mutex_owned(&value);  }
#endif /* GU_DEBUG_MUTEX */

    protected:

        gu_mutex_t mutable value;

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
