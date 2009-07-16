#ifndef MONITOR_HPP
#define MONITOR_HPP



#include <galeracomm/exception.hpp>

#include <pthread.h>
#include <deque>
#include <list>
#include <cassert>

class Waiter {
    pthread_cond_t cond;
    unsigned long id;
public:
    Waiter(unsigned long i) : id(i) {
	pthread_cond_init(&cond, 0);
    }
    void wait(pthread_mutex_t *mutex) {
	if (pthread_cond_wait(&cond, mutex))
	    throw DException("");
    }
    void signal() {
	if (pthread_cond_signal(&cond))
	    throw DException("");
    }
    unsigned long get_id() const {
	return id;
    }
};

class Monitor {
    pthread_mutex_t mutex;
    std::list<Waiter *> waiters;
    bool busy;
    unsigned long last_id;
    static bool skip_locking;
public:

    static void set_skip_locking(const bool val) {
	skip_locking = val;
    }
    
    Monitor() : busy(false), last_id(0) {
	if (skip_locking == false)
	    pthread_mutex_init(&mutex, 0);
    }
    void enter() {
	if (skip_locking)
	    return;
	if (pthread_mutex_lock(&mutex))
	    throw DException("");
	if (busy) {
	    Waiter w(last_id++);
	    waiters.push_back(&w);
	    w.wait(&mutex);
	    assert(waiters.front()->get_id() == w.get_id());
	    waiters.pop_front();
	} else {
	    busy = true;
	}
	assert(busy == true);
	if (pthread_mutex_unlock(&mutex))
	    throw DException("");
    }
    void leave() {
	if (skip_locking)
	    return;
	assert(busy == true);
	if (pthread_mutex_lock(&mutex))
	    throw DException("");
	if (waiters.size())
	    waiters.front()->signal();
	else
	    busy = false;
	if (pthread_mutex_unlock(&mutex))
	    throw DException("");
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

#endif // MONITOR_HPP
