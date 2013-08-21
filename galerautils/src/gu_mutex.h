// Copyright (C) 2007 Codership Oy <info@codership.com>

/**
 * @file Special mutex replacements for debugging/porting
 *
 * $Id$
 */
 
#ifndef _gu_mutex_h_
#define _gu_mutex_h_

#include <pthread.h>

struct gu_mutex
{
    pthread_mutex_t  target_mutex;      //!< for critical section
    pthread_mutex_t  control_mutex;     //!< for mutex operations

    volatile int         lock_waiter_count; //!< # of threads waiting for lock
    volatile int         cond_waiter_count; //!< # of threads waiting for cond
    volatile int         holder_count;      //!< must be 0 or 1
    volatile pthread_t   thread;

    /* point in source code, where called from */
    volatile const char *file;
    volatile int         line;
};

/** @name Usual mutex operations storing FILE and LINE information */
/*@{*/
int gu_mutex_init_dbg    (struct gu_mutex *mutex,
                          const pthread_mutexattr_t *attr,
                          const char *file, unsigned int line);
int gu_mutex_lock_dbg    (struct gu_mutex *mutex,
                          const char *file, unsigned int line);
int gu_mutex_unlock_dbg  (struct gu_mutex *mutex,
                          const char *file, unsigned int line);
int gu_mutex_destroy_dbg (struct gu_mutex *mutex,
                          const char *file, unsigned int line);
int gu_cond_wait_dbg     (pthread_cond_t *cond,
                          struct gu_mutex *mutex, 
                          const char *file, unsigned int line);
/*@}*/

/** Shorter mutex API for applications.
 *  Depending on compile-time flags application will either use 
 *  debug or normal version of the mutex API */
/*@{*/
#ifdef DEBUG_MUTEX

typedef struct gu_mutex gu_mutex_t;

#define gu_mutex_init(M,A)  gu_mutex_init_dbg   ((M),(A), __FILE__, __LINE__)
#define gu_mutex_lock(M)    gu_mutex_lock_dbg   ((M), __FILE__, __LINE__)
#define gu_mutex_unlock(M)  gu_mutex_unlock_dbg ((M), __FILE__, __LINE__)
#define gu_mutex_destroy(M) gu_mutex_destroy_dbg((M), __FILE__, __LINE__)
#define gu_cond_wait(S,M)   gu_cond_wait_dbg    ((S),(M), __FILE__, __LINE__)

#define GU_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER,       \
            PTHREAD_MUTEX_INITIALIZER,                          \
            0,0,0,0,0,0 }

#else /* DEBUG_MUTEX not defined - use regular pthread functions */

typedef pthread_mutex_t gu_mutex_t;

#define gu_mutex_init(M,A)  pthread_mutex_init   ((M),(A))
#define gu_mutex_lock(M)    pthread_mutex_lock   ((M))
#define gu_mutex_unlock(M)  pthread_mutex_unlock ((M))
#define gu_mutex_destroy(M) pthread_mutex_destroy((M))
#define gu_cond_wait(S,M)   pthread_cond_wait    ((S),(M))

#define GU_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

#endif /* DEBUG_MUTEX */
/*@}*/

/* The following typedefs and macros don't do anything now,
 * but may be used later */
typedef pthread_t         gu_thread_t;
typedef pthread_cond_t    gu_cond_t;
#define gu_thread_create  pthread_create
#define gu_thread_join    pthread_join
#define gu_thread_cancel  pthread_cancel
#define gu_thread_exit    pthread_exit
#define gu_cond_init      pthread_cond_init
#define gu_cond_destroy   pthread_cond_destroy
#define gu_cond_signal    pthread_cond_signal
#define gu_cond_broadcast pthread_cond_broadcast
#define gu_cond_timedwait pthread_cond_timedwait

#if defined(__APPLE__)

#ifdef __cplusplus
extern "C" {
#endif

typedef int pthread_barrierattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
} pthread_barrier_t;

int pthread_barrier_init (pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy (pthread_barrier_t *barrier);
int pthread_barrier_wait (pthread_barrier_t *barrier);

#ifdef __cplusplus
}
#endif

#endif /* __APPLE__ */

#endif /* _gu_mutex_h_ */
