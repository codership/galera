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
        Mutex (const Mutex&);
	Mutex& operator= (const Mutex&);

        friend class Lock;

    protected:

        pthread_mutex_t mutable value;

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
    };
}

#endif /* __GU_MUTEX__ */
