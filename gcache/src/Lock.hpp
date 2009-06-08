/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GCACHE_LOCK__
#define __GCACHE_LOCK__

#include <pthread.h>
#include <cerrno>

#include "Exception.hpp"
#include <galerautils.hpp>
#include "Mutex.hpp"
#include "Cond.hpp"

namespace gcache
{
    class Lock
    {

    private:

        pthread_mutex_t* value;

    public:

        Lock (Mutex& mtx)
        {
            value = &mtx.value;

            int err = pthread_mutex_lock (value);

            if (err) {
                std::string msg = "Mutex lock failed: ";
                msg = msg + strerror(err);
                throw Exception(msg.c_str(), err);
            }

        };

        virtual ~Lock ()
        {
            pthread_mutex_unlock (value);
            log_debug << "Unlocked mutex " << value;
        };

        inline void wait (Cond& cond)
        {
            cond.ref_count++;
            pthread_cond_wait (&(cond.cond), value);
            cond.ref_count--;
        };

    };
}

#endif /* __GCACHE_LOCK__ */
