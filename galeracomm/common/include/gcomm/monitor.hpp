#ifndef MONITOR_HPP
#define MONITOR_HPP



#include <gcomm/exception.hpp>

#include <pthread.h>
#include <deque>

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
    std::deque<Waiter *> waiters;
    bool busy;
    unsigned long last_id;
public:
    Monitor() : busy(false), last_id(0) {
	pthread_mutex_init(&mutex, 0);
    }
    void enter() {
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
