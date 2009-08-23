#ifndef MONITOR_HPP
#define MONITOR_HPP



#include <galerautils.hpp>

#include <pthread.h>
#include <deque>
#include <list>
#include <cassert>

class Waiter {
    gu::Cond      cond;
    unsigned long id;
public:
    Waiter(unsigned long i) : cond(), id(i) {}

    void wait(gu::Lock &lock) {
	lock.wait(cond);
    }

    void signal() {
	cond.signal();
    }

    unsigned long get_id() const {
	return id;
    }
};

class Monitor {

    gu::Mutex          mutex;
    std::list<Waiter*> waiters;
    bool               busy;
    unsigned long      last_id;
    static bool        skip_locking;

public:

    static void set_skip_locking(const bool val) {
	skip_locking = val;
    }
    
    Monitor() : mutex(), waiters(), busy(false), last_id(0) {}

    void enter()
    {
	if (skip_locking) return;

        gu::Lock lock(mutex);

	if (busy) {
	    Waiter w(last_id++);
	    waiters.push_back(&w);
	    w.wait(lock);
	    assert(waiters.front()->get_id() == w.get_id());
	    waiters.pop_front();
	} else {
	    busy = true;
	}
	assert(busy == true);
    }

    void leave()
    {
	if (skip_locking) return;
	assert(busy == true);

        gu::Lock lock(mutex);

	if (waiters.size())
	    waiters.front()->signal();
	else
	    busy = false;
    }
};


class Critical {

    Monitor *mon;

    Critical(const Critical& c);
    void operator=(const Critical& c);

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

#endif // MONITOR_HPP
