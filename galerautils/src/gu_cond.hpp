/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef __GU_COND__
#define __GU_COND__

#include <pthread.h>
#include <unistd.h>
#include <cerrno>

#include "gu_macros.h"
#include "gu_exception.hpp"

// TODO: make exceptions more verbose

namespace gu
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
	    register int ret;
            while (EBUSY == (ret = pthread_cond_destroy(&cond)))
		{ usleep (100); }
            if (gu_unlikely(ret != 0))
                throw Exception("pthread_cond_destroy() failed", ret);
        };

        inline void signal ()
        {
            if (ref_count > 0) {
                register int ret = pthread_cond_signal (&cond);
                if (gu_unlikely(ret != 0))
                    throw Exception("pthread_cond_signal() failed", ret);
            }
        }

        inline void broadcast ()
        {
            if (ref_count > 0) {
                register int ret = pthread_cond_broadcast (&cond);
                if (gu_unlikely(ret != 0))
                    throw Exception("pthread_cond_broadcast() failed", ret);
            }
        }

    };
}

#endif // __GU_COND__
