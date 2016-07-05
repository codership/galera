/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#ifndef __GU_MUTEX__
#define __GU_MUTEX__

#include <pthread.h>
#include <cerrno>
#include <cstring>

#include "gu_macros.h"
#include "gu_mutex.h"
#include "gu_throw.hpp"

namespace gu
{
    class Mutex
    {
    public:

        Mutex () : value()
        {
            gu_mutex_init (&value, NULL); // always succeeds
        }

        ~Mutex ()
        {
            int err = gu_mutex_destroy (&value);
            if (gu_unlikely(err != 0))
            {
                gu_throw_error (err) << "pthread_mutex_destroy()";
            }
        }

        void lock()
        {
            gu_mutex_lock(&value);
        }

        void unlock()
        {
            gu_mutex_unlock(&value);
        }

    protected:

        gu_mutex_t mutable value;

    private:

        Mutex (const Mutex&);
        Mutex& operator= (const Mutex&);

        friend class Lock;
    };

#ifdef HAVE_PSI_INTERFACE

    /* MutexWithPFS can be instrumented with MySQL performance schema.
    (provided mysql has performance schema enabled).
    In order to faciliate instead of direclty creating instance of
    pthread mutex, mysql mutex instances is created using callback.
    MutexWithPFS just act as wrapper invoking appropriate calls from
    MySQL space. */
    class MutexWithPFS
    {
    public:

        MutexWithPFS (wsrep_pfs_instr_tag_t tag) : value(), m_tag (tag)
        {
            pfs_instr_callback(WSREP_PFS_INSTR_TYPE_MUTEX,
                               WSREP_PFS_INSTR_OPS_INIT,
                               m_tag, reinterpret_cast<void**> (&value),
                               NULL, NULL);
        }

        ~MutexWithPFS ()
        {
            pfs_instr_callback(WSREP_PFS_INSTR_TYPE_MUTEX,
                               WSREP_PFS_INSTR_OPS_DESTROY,
                               m_tag, reinterpret_cast<void**> (&value),
                               NULL, NULL);
        }

        void lock()
        {
            pfs_instr_callback(WSREP_PFS_INSTR_TYPE_MUTEX,
                               WSREP_PFS_INSTR_OPS_LOCK,
                               m_tag, reinterpret_cast<void**> (&value),
                               NULL, NULL);
        }

        void unlock()
        {
            pfs_instr_callback(WSREP_PFS_INSTR_TYPE_MUTEX,
                               WSREP_PFS_INSTR_OPS_UNLOCK,
                               m_tag, reinterpret_cast<void**> (&value),
                               NULL, NULL);
        }

   protected:

        gu_mutex_t* value;

    private:

        wsrep_pfs_instr_tag_t m_tag;

        MutexWithPFS (const MutexWithPFS&);
        MutexWithPFS& operator= (const MutexWithPFS&);

        friend class Lock;
    };
#endif /* HAVE_PSI_INTERFACE */

    class RecursiveMutex
    {
    public:
        RecursiveMutex() : mutex_()
        {
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&mutex_, &mattr);
            pthread_mutexattr_destroy(&mattr);
        }
        
        ~RecursiveMutex()
        {
            pthread_mutex_destroy(&mutex_);
        }
        
        void lock()
        {
            if (pthread_mutex_lock(&mutex_)) gu_throw_fatal;
        }
        
        void unlock()
        {
            if (pthread_mutex_unlock(&mutex_)) gu_throw_fatal;
        }
        
    private:
        RecursiveMutex(const RecursiveMutex&);
        void operator=(const RecursiveMutex&);
        
        pthread_mutex_t mutex_;
    };


}

#endif /* __GU_MUTEX__ */
