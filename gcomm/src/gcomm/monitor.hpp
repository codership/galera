#ifndef MONITOR_HPP
#define MONITOR_HPP


#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/mutex.hpp>

#include <pthread.h>
#include <deque>
#include <list>
#include <cassert>

using std::list;

BEGIN_GCOMM_NAMESPACE

class Waiter {
    Cond cond;
    unsigned long id;
public:
    Waiter(unsigned long i) : 
        id(i) 
    {

    }
    
    void wait(Mutex *mutex) 
    {
        cond.wait(mutex);
    }

    void signal() 
    {
        cond.signal();
    }
    
    unsigned long get_id() const {
	return id;
    }
};

class Monitor 
{
    mutable Mutex mutex;
    list<Waiter *> waiters;
    bool busy;
    unsigned long last_id;
    pthread_t holder;
    unsigned long refcnt;
    static bool skip_locking;
public:
    
    static void set_skip_locking(const bool val) 
    {
	skip_locking = val;
    }
    
    Monitor() : 
        busy(false), 
        last_id(0), 
        holder(0),
        refcnt(0)
    {
    }

    void enter() 
    {
	if (skip_locking)
	    return;
        Lock lock(&mutex);
        
        if (busy && holder == pthread_self())
        {
            refcnt++;
            return;
        }
        
	if (busy) {
	    Waiter w(last_id++);
	    waiters.push_back(&w);
	    w.wait(&mutex);
	    assert(waiters.front()->get_id() == w.get_id());
	    waiters.pop_front();
	} else {
	    busy = true;
	}
        refcnt = 1;
        holder = pthread_self();
	assert(busy == true);
    }
    
    void leave() 
    {
	if (skip_locking)
	    return;
        Lock lock(&mutex);
	assert(busy == true && holder == pthread_self());
        --refcnt;
        if (refcnt > 0)
        {
            return;
        }

	if (waiters.size())
	    waiters.front()->signal();
	else
	    busy = false;
        holder = 0;
    }

    unsigned long get_refcnt() const
    {
        Lock lock(&mutex);
        assert(holder == pthread_self());
        return refcnt;
    }

};


class Critical {
    Monitor *mon;
public:
    Critical(Monitor *m) : mon(m) {
	if (mon)
	    mon->enter();
    }
    ~Critical() {
	if (mon)
	    mon->leave();
    }
};

END_GCOMM_NAMESPACE

#endif // MONITOR_HPP
