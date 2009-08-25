#ifndef THREAD_HPP
#define THREAD_HPP

#include <pthread.h>
#include <galeracomm/exception.hpp>
#include <galeracomm/logger.hpp>
#include <galeracomm/monitor.hpp>


class Thread {
    pthread_t th;
    Monitor monitor;
    enum State {
	STOPPED, 
	RUNNING,
	CANCELED
    };
    State state;

public:
    Thread() : th(0), monitor(), state(STOPPED) {
	
    }

    virtual ~Thread() {

    }
    
    virtual void run() = 0;
    
    static void* start_fn(void *arg) {
	static_cast<Thread*>(arg)->run();
	pthread_exit(0);
    }
    
    void start() {
	int err;
	
	Critical crit(&monitor);
	if (state != STOPPED)
	    throw FatalException("Tread::start(): invalid state");
	
	if ((err = pthread_create(
		 &th, 0, reinterpret_cast<void* (*)(void*)>(&Thread::start_fn), this))) {
	    LOG_FATAL(std::string("Thread::start(): pthread_create(): ") + ::strerror(err));
	    throw FatalException("Thread::start(): Couldn't start thread");
	}
	state = RUNNING;
    }
    
    void stop() {
	int err;
	Critical crit(&monitor);
	if (state != RUNNING)
	    throw FatalException("Thread::stop(): invalid state");

	state = CANCELED;
	if ((err = pthread_cancel(th))) {
	    LOG_FATAL(
		std::string("Thread::stop(): pthread_cancel(): ") + 
		::strerror(err));
	}
	if ((err = pthread_join(th, 0))) {
	    LOG_FATAL(
		std::string("Thread::stop(): pthread_join(): ") + 
		::strerror(err));
	    throw FatalException("Thread::stop(): join failed");
	}
	state = STOPPED;
    }
};


#endif // THREAD_HPP
