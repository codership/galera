/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_MUTEX__
#define __GCACHE_MUTEX__

#include <pthread.h>
#include <cerrno>

#include "Exception.hpp"
#include <galerautils.hpp>

namespace gcache
{
    class Mutex
    {

        friend class Lock;

    protected:

        pthread_mutex_t value;

    public:

        Mutex ()
        {
            pthread_mutex_init (&value, NULL);
        };

        virtual ~Mutex ()
        {
            int err = pthread_mutex_destroy (&value);
            if (gu_unlikely(err != 0)) {
                log_fatal << "pthread_mutex_destroy() failed: " << err
                          << " (" << strerror(err) << "). Aborting.";
                ::abort();
            }
            log_debug << "Destroyed mutex " << &value;
        };
    };
}

#endif /* __GCACHE_MUTEX__ */
