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
#include "gu_datetime.hpp"

namespace gu
{
    class Lock
    {
        pthread_mutex_t* const value;

#ifdef HAVE_PSI_INTERFACE
        gu::MutexWithPFS* const mutex;
#endif /* HAVE_PSI_INTERFACE */

        Lock (const Lock&);
        Lock& operator=(const Lock&);

    public:

        Lock (const Mutex& mtx) : value(&mtx.value)
#if HAVE_PSI_INTERFACE
            , mutex()
#endif
        {
            int err = pthread_mutex_lock (value);
            if (gu_unlikely(err))
            {
                std::string msg = "Mutex lock failed: ";
                msg = msg + strerror(err);
                throw Exception(msg.c_str(), err);
            }
        }

        virtual ~Lock ()
        {
#ifdef HAVE_PSI_INTERFACE
            if (mutex != NULL)
            {
                mutex->unlock();
                return;
            }
#endif /* HAVE_PSI_INTERFACE */
            int err = pthread_mutex_unlock (value);
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
            pthread_cond_wait (&(cond.cond), value);
            cond.ref_count--;
        }

        inline void wait (const Cond& cond, const datetime::Date& date)
        {
            timespec ts;

            date._timespec(ts);
            cond.ref_count++;
            int ret = pthread_cond_timedwait (&(cond.cond), value, &ts);
            cond.ref_count--;

            if (gu_unlikely(ret)) gu_throw_error(ret);
        }

#ifdef HAVE_PSI_INTERFACE
        Lock (const MutexWithPFS& mtx)
            :
            value(mtx.value),
            mutex(const_cast<gu::MutexWithPFS*>(&mtx))
        {
            mutex->lock();
        }

        inline void wait (const CondWithPFS& cond)
        {
            cond.ref_count++;
            pfs_instr_callback(
                WSREP_PFS_INSTR_TYPE_CONDVAR,
                WSREP_PFS_INSTR_OPS_WAIT,
                cond.m_tag,
                reinterpret_cast<void**>(const_cast<gu_cond_t**>(&(cond.cond))),
                reinterpret_cast<void**>(const_cast<pthread_mutex_t**>(&value)),
                NULL);
            cond.ref_count--;
        }

        inline void wait (const CondWithPFS& cond, const datetime::Date& date)
        {
            timespec ts;

            date._timespec(ts);
            cond.ref_count++;
            pfs_instr_callback(
                WSREP_PFS_INSTR_TYPE_CONDVAR,
                WSREP_PFS_INSTR_OPS_WAIT,
                cond.m_tag,
                reinterpret_cast<void**>(const_cast<gu_cond_t**>(&(cond.cond))),
                reinterpret_cast<void**>(const_cast<pthread_mutex_t**>(&value)),
                &ts);
            cond.ref_count--;
        }
#endif /* HAVE_PSI_INTERFACE */
    };
}

#endif /* __GU_LOCK__ */
