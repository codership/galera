#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <pthread.h>
#include <gcomm/exception.hpp>
#include <gcomm/logger.hpp>

class Mutex {
    pthread_mutex_t mutex;
public:
    
    Mutex() {
	if (pthread_mutex_init(&mutex, 0))
	    throw FatalException("Mutex(): init failed");
    }
    
    ~Mutex() {
	if (pthread_mutex_destroy(&mutex))
	    throw FatalException("~Mutex(): destroy failed");
    }
    
    void lock() {
	int err;
	if ((err = pthread_mutex_lock(&mutex))) {
	    LOG_FATAL(
		std::string("Mutex::lock(): pthread_mutex_lock() failed: ") + 
		::strerror(errno));
	    throw FatalException("Mutex::lock(): lock failed");
	}
    }
    
    void unlock() {
	int err;
	if ((err = pthread_mutex_unlock(&mutex))) {
	    LOG_FATAL(
		std::string("Mutex::unlock(): pthread_mutex_unlock(): ") + 
		::strerror(errno));
	    throw FatalException("Mutex::unlock(): unlock failed");
	}	
    }
};

#endif // MUTEX_HPP
