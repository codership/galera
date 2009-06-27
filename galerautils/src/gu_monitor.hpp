/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file gu_monitor.hpp
 *
 *
 */

#ifndef __GU_MONITOR_HPP__
#define __GU_MONITOR_HPP__

#include <gu_lock.hpp>

#include <cassert>

namespace gu
{
    class Monitor;
    class Critical;
}

class gu::Monitor
{
    int refcnt;
    /* TODO: */
    pthread_t holder;
    Mutex mutex;
    Cond cond;

public:
    Monitor() :
        refcnt(0),
        holder(0)
    {
    }
    ~Monitor()
    {
    }

    void enter()
    {
        Lock lock(mutex);
        while (refcnt > 0 && pthread_equal(holder, pthread_self()) == 0)
        {
            lock.wait(cond);
        }
        refcnt++;
        holder = pthread_self();
    }
    
    void leave()
    {
        Lock lock(mutex);
        assert(refcnt > 0);
        assert(pthread_equal(holder, pthread_self()) != 0);
        refcnt--;
        if (refcnt == 0)
        {
            cond.signal();
        }
    }
};

class gu::Critical
{
    Monitor& mon;
public:
    Critical(Monitor& mon_) :
        mon(mon_)
    {
        mon.enter();
    }

    ~Critical()
    {
        mon.leave();
    }
};


#endif /* __GU_MONITOR_HPP__ */
