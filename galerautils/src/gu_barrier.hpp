//
// Copyright (C) 2016-2017 Codership Oy <info@codership.com>
//


#ifndef GU_BARRIER
#define GU_BARRIER

#include <gu_threads.h>
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
            if ((err = gu_barrier_init(&barrier_, 0, count)) != 0)
            {
                gu_throw_error(err) << "Barrier init failed";
            }
        }

        ~Barrier()
        {
            int err;
            if ((err = gu_barrier_destroy(&barrier_)) != 0)
            {
                assert(0);
                log_warn << "Barrier destroy failed: " << ::strerror(err);
            }
        }

        void wait()
        {
            int err(gu_barrier_wait(&barrier_));
            if (err != 0 && err != GU_BARRIER_SERIAL_THREAD)
            {
                gu_throw_error(err) << "Barrier wait failed";
            }
        }

    private:
        // Non-copyable
        Barrier(const Barrier&);
        Barrier& operator=(const Barrier&);
        gu_barrier_t barrier_;
    };
}


#endif // GU_BARRIER
