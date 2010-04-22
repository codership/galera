/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include <pthread.h>
#include <cerrno>
#include <cstring>

#include "gu_macros.h"
#include "gu_throw.hpp"

namespace gu
{
    class Mutex
    {
    public:

        Mutex () : value()
        {
            pthread_mutex_init (&value, NULL); // always succeeds
        }

        ~Mutex ()
        {
            int err = pthread_mutex_destroy (&value);
            if (gu_unlikely(err != 0))
            {
                gu_throw_error (err) << "pthread_mutex_destroy()";
            }
        }

        void lock()
        {
            pthread_mutex_lock(&value);
        }

        void unlock()
        {
            pthread_mutex_unlock(&value);
        }

    protected:

        pthread_mutex_t mutable value;

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
        RecursiveMutex(const Mutex&);
        void operator=(const Mutex&);
        
        pthread_mutex_t mutex_;
    };


}

#endif /* __GU_MUTEX__ */
