#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <gcomm/common.hpp>
#include <gcomm/types.hpp>
#include <pthread.h>
#include <gcomm/exception.hpp>

BEGIN_GCOMM_NAMESPACE

class Mutex
{
    pthread_mutex_t mutex;
public:
    
    Mutex()
    {
	if (pthread_mutex_init(&mutex, 0))
	    throw FatalException("Mutex(): init failed");
    }
    
    ~Mutex()
    {
	if (pthread_mutex_destroy(&mutex))
	    throw FatalException("~Mutex(): destroy failed");
    }
    
    void lock()
    {
	int err;
	if ((err = pthread_mutex_lock(&mutex)))
        {
	    throw FatalException("Mutex::lock(): lock failed");
	}
    }
    
    void unlock()
    {
	int err;
	if ((err = pthread_mutex_unlock(&mutex)))
        {
	    throw FatalException("Mutex::unlock(): unlock failed");
	}	
    }

    pthread_mutex_t* get()
    {
        return &mutex;
    }

};


class Cond
{
    pthread_cond_t cond;
public:
    Cond()
    {
        pthread_cond_init(&cond, 0);
    }
    
    ~Cond()
    {
        pthread_cond_destroy(&cond);
    }
    
    void wait(Mutex* m)
    {
        if (pthread_cond_wait(&cond, m->get()))
        {
            throw FatalException("");
        }
    }
    
    void signal()
    {
        if (pthread_cond_signal(&cond))
        {
            throw FatalException("");
        }
    }


};

class Lock
{
    Mutex* mutex;
public:
    Lock(Mutex* mutex_) :
        mutex(mutex_)
    {
        mutex->lock();
    }

    ~Lock()
    {
        mutex->unlock();
    }
    
};


END_GCOMM_NAMESPACE

#endif // MUTEX_HPP
