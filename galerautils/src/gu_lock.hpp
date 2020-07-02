/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_LOCK__
#define __GU_LOCK__

#include "gu_exception.hpp"
#include "gu_logger.hpp"
#include "gu_mutex.hpp"
#include "gu_cond.hpp"
#include "gu_datetime.hpp"

#include <cerrno>
#include <cassert>

namespace gu
{
    class Lock
    {
        const Mutex& mtx_;

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
#ifdef GU_MUTEX_DEBUG
            mtx_.locked_ = false;
            mtx_.disown();
#endif /* GU_MUTEX_DEBUG */
            cond.ref_count++;
            gu_cond_wait (&(cond.cond), &mtx_.impl()); // never returns error
            cond.ref_count--;
#ifdef GU_MUTEX_DEBUG
            mtx_.locked_ = true;
            mtx_.owned_  = gu_thread_self();
#endif /* GU_MUTEX_DEBUG */
        }

        inline void wait (const Cond& cond, const datetime::Date& date)
        {
            timespec ts;

            date._timespec(ts);
#ifdef GU_MUTEX_DEBUG
            mtx_.locked_ = false;
            mtx_.disown();
#endif /* GU_MUTEX_DEBUG */
            cond.ref_count++;
            int const ret(gu_cond_timedwait (&(cond.cond), &mtx_.impl(), &ts));
            cond.ref_count--;
#ifdef GU_MUTEX_DEBUG
            mtx_.locked_ = true;
            mtx_.owned_  = gu_thread_self();
#endif /* GU_MUTEX_DEBUG */

            if (gu_unlikely(ret)) gu_throw_error(ret);
        }
    };
}

#endif /* __GU_LOCK__ */
