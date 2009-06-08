/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GCACHE_COND__
#define __GCACHE_COND__

#include <pthread.h>
#include <unistd.h>
#include <cerrno>

namespace gcache
{
    class Cond
    {

        friend class Lock;

    protected:

        pthread_cond_t cond;
        long           ref_count;

    public:

        Cond () throw()
        : ref_count(0)
        { pthread_cond_init (&cond, NULL); };

        ~Cond ()
        {
            while (EBUSY == pthread_cond_destroy(&cond)) { usleep (100); };
        };

        inline void signal ()
        {
            if (ref_count > 0) pthread_cond_signal (&cond);
        }

        inline void broadcast ()
        {
            if (ref_count > 0) pthread_cond_broadcast (&cond);
        }

    };
}

#endif
