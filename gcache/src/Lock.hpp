/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_LOCK__
#define __GCACHE_LOCK__

#include <pthread.h>

namespace gcache
{
    class Lock
    {
    private:
        pthread_mutex_t* mutex;

    public:
        Lock (pthread_mutex_t* mtx)
        {
            int err;

            mutex = mtx;
            err = pthread_mutex_lock (mutex);

            if (err) {
                throw Exception("Mutex lock failed");
            }
        };

        virtual ~Lock ()
        {
            pthread_mutex_unlock (mutex);
        };
    }
}

#endif __GCACHE_LOCK__
