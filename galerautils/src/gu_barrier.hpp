//
// Copyright (C) 2016 Codership Oy <info@codership.com>
//


#ifndef GU_BARRIER
#define GU_BARRIER

#include <pthread.h>
#include "gu_throw.hpp"


namespace gu
{
    class Barrier
    {
    public:
        Barrier(unsigned count)
            :
            barrier_()
        {
            int err;
            if ((err = pthread_barrier_init(&barrier_, 0, count)) != 0)
            {
                gu_throw_error(err) << "Barrier init failed";
            }
        }

        ~Barrier()
        {
            int err;
            if ((err = pthread_barrier_destroy(&barrier_)) != 0)
            {
                assert(0);
                log_warn << "Barrier destroy failed: " << ::strerror(err);
            }
        }

        void wait()
        {
            int err(pthread_barrier_wait(&barrier_));
            if (err != 0 && err != PTHREAD_BARRIER_SERIAL_THREAD)
            {
                gu_throw_error(err) << "Barrier wait failed";
            }
        }

    private:
        // Non-copyable
        Barrier(const Barrier&);
        Barrier& operator=(const Barrier&);
        pthread_barrier_t barrier_;
    };
}


#endif // GU_BARRIER
