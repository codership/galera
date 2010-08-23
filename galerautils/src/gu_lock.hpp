/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_LOCK__
#define __GU_LOCK__

#include <pthread.h>
#include <cerrno>

#include "gu_exception.hpp"
#include "gu_logger.hpp"
#include "gu_mutex.hpp"
#include "gu_cond.hpp"

namespace gu
{
    class Lock
    {
        pthread_mutex_t* const value;

        Lock (const Lock&);
        Lock& operator=(const Lock&);

    public:

        Lock (const Mutex& mtx) : value(&mtx.value)
        {

            int err = pthread_mutex_lock (value);
            if (err)
            {
                std::string msg = "Mutex lock failed: ";
                msg = msg + strerror(err);
                throw Exception(msg.c_str(), err);
            }
        }

        virtual ~Lock ()
        {
            int err = pthread_mutex_unlock (value);
            if (err)
            {
                std::string msg = "Mutex unlock failed: ";
                msg = msg + strerror(err);
                throw Exception(msg.c_str(), err);
            }
            // log_debug << "Unlocked mutex " << value;
        }

        inline void wait (const Cond& cond)
        {
            cond.ref_count++;
            pthread_cond_wait (&(cond.cond), value);
            cond.ref_count--;
        }

    };
}

#endif /* __GU_LOCK__ */
