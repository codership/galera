/*
 * Copyright (C) 2009-2015 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_LOCK__
#define __GU_LOCK__

#include "gu_mutex.hpp"
#include "gu_cond.hpp"
#include "gu_datetime.hpp"

namespace gu
{
    class Lock
    {
        const gu::Mutex& mtx_;

        Lock (const Lock&);
        Lock& operator=(const Lock&);

    public:

        Lock (const Mutex& mtx) : mtx_(mtx)
        {
            mtx_.lock();
        }

        virtual ~Lock ()
        {
            mtx_.unlock();
        }

        inline void wait (const Cond& cond)
        {
            cond.ref_count++;
            gu_cond_wait (&(cond.cond), &(mtx_.value_));
            cond.ref_count--;
#ifndef NDEBUG
            mtx_.locked_ = true;
#endif /* NDEBUG */
        }

        inline void wait (const Cond& cond, const datetime::Date& date)
        {
            timespec ts;

            date._timespec(ts);
            cond.ref_count++;
            int const ret(gu_cond_timedwait(&(cond.cond), &(mtx_.value_), &ts));
            cond.ref_count--;
#ifndef NDEBUG
            mtx_.locked_ = true;
#endif /* NDEBUG */

            if (gu_unlikely(ret)) gu_throw_error(ret);
        }
    };
}

#endif /* __GU_LOCK__ */
