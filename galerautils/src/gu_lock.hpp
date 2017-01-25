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

            int const err(mtx_.lock());
            if (gu_unlikely(err))
            {
                std::string msg = "Mutex lock failed: ";
                msg = msg + strerror(err);
                throw Exception(msg.c_str(), err);
            }
        }

        virtual ~Lock ()
        {
#ifdef GU_DEBUG_MUTEX
            assert(mtx_.owned());
#endif
            int const err(mtx_.unlock());
            if (gu_unlikely(err))
            {
                log_fatal << "Mutex unlock failed: " << err << " ("
                          << strerror(err) << "), Aborting.";
                ::abort();
            }
            // log_debug << "Unlocked mutex " << value;
        }

        inline void wait (const Cond& cond)
        {
            cond.ref_count++;
            gu_cond_wait (&(cond.cond), &mtx_.impl());
            cond.ref_count--;
        }

        inline void wait (const Cond& cond, const datetime::Date& date)
        {
            timespec ts;

            date._timespec(ts);
            cond.ref_count++;
            int ret = gu_cond_timedwait (&(cond.cond), &mtx_.impl(), &ts);
            cond.ref_count--;

            if (gu_unlikely(ret)) gu_throw_error(ret);
        }
    };
}

#endif /* __GU_LOCK__ */
