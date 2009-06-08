/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include <pthread.h>
#include <cerrno>

#include "gu_exception.hpp"
#include "gu_logger.hpp"

namespace gu
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
            if (err != 0) {
                throw Exception (strerror(err), err);
            }
            log_debug << "Destroyed mutex " << &value;
        };
    };
}

#endif /* __GU_MUTEX__ */
