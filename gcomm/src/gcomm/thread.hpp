#ifndef _GCOMM_THREAD_HPP_
#define _GCOMM_THREAD_HPP_

#include <pthread.h>
#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/logger.hpp>
#include <gcomm/monitor.hpp>

BEGIN_GCOMM_NAMESPACE

class Thread
{
    pthread_t th;
    Monitor monitor;
    enum State {
	STOPPED, 
	RUNNING,
	CANCELED
    };
    State state;

public:
    Thread() : 
        th(),
        monitor(),
        state(STOPPED)
    {
	
    }
    
    virtual ~Thread()
    {
        
    }
    
    virtual void run() = 0;
    
    static void* start_fn(void *arg)
    {
	static_cast<Thread*>(arg)->run();
	pthread_exit(0);
    }
    
    void start()
    {
	Critical crit(&monitor);
        
	int err;
	if (state != STOPPED)
	    throw FatalException("Tread::start(): invalid state");
	
	if ((err = pthread_create(
		 &th, 0, 
                 reinterpret_cast<void* (*)(void*)>(&Thread::start_fn), this))) 
        {
	    LOG_FATAL(std::string("Thread::start(): pthread_create(): ") 
                      + ::strerror(err));
	    throw FatalException("Thread::start(): Couldn't start thread");
	}
	state = RUNNING;
    }
    
    void stop()
    {
	Critical crit(&monitor);
        
	int err;
	if (state != RUNNING)
        {
	    throw FatalException("Thread::stop(): invalid state");
        }
        
	state = CANCELED;
	if ((err = pthread_cancel(th)))
        {
	    LOG_DEBUG(string("pthread_cancel(): ") + ::strerror(err));
	}
        
	if ((err = pthread_join(th, 0)))
        {
	    LOG_FATAL(
		std::string("Thread::stop(): pthread_join(): ") + 
		::strerror(err));
	    throw FatalException("Thread::stop(): join failed");
	}
	state = STOPPED;
    }
};

END_GCOMM_NAMESPACE

#endif // _GCOMM_THREAD_HPP_
