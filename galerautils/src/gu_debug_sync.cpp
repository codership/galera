//
// Copyright (C) 2014 Codership Oy <info@codership.com>
//

#ifdef GU_DBUG_ON

#include "gu_debug_sync.hpp"
#include "gu_lock.hpp"
#include <map>


namespace {
    gu::Mutex sync_mutex;
    typedef std::multimap<std::string, gu::Cond*> SyncMap;
    SyncMap sync_waiters;
}



void gu_debug_sync_wait(const std::string& sync)
{
    gu::Lock lock(sync_mutex);
    gu::Cond cond;
    log_debug << "enter sync wait '" << sync << "'";
    SyncMap::iterator i(
        sync_waiters.insert(std::make_pair(sync, &cond)));
    lock.wait(cond);
    sync_waiters.erase(i);
    log_debug << "leave sync wait '" << sync << "'";
}


void gu_debug_sync_signal(const std::string& sync)
{
    gu::Lock lock(sync_mutex);
    std::pair<SyncMap::iterator, SyncMap::iterator>
        range(sync_waiters.equal_range(sync));
    for (SyncMap::iterator i(range.first); i != range.second; ++i)
    {
        log_debug << "signalling waiter";
        i->second->signal();
    }
}


std::string gu_debug_sync_waiters()
{
    std::string ret;
    gu::Lock lock(sync_mutex);
    for (SyncMap::iterator i(sync_waiters.begin());
         i != sync_waiters.end();)
    {
        ret += i->first;
        ++i;
        if (i != sync_waiters.end()) ret += " ";
    }
    return ret;
}

#endif // GU_DBUG_ON
