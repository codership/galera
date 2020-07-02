// Copyright (C) 2017 Codership Oy <info@codership.com>

/**
 * @file Abstracts naitive multithreading API behind POSIX threads-like API
 */

#ifndef _gu_mutex_h_
#define _gu_mutex_h_

#include "gu_types.h" // bool

#if __unix__

#include <pthread.h>

typedef pthread_t             gu_thread_t_SYS;
#define gu_thread_create_SYS  pthread_create
#define gu_thread_join_SYS    pthread_join
#define gu_thread_cancel_SYS  pthread_cancel
#define gu_thread_exit_SYS    pthread_exit
#define gu_thread_detach_SYS  pthread_detach
#define gu_thread_self_SYS    pthread_self
#define gu_thread_equal_SYS   pthread_equal

#define GU_THREAD_INITIALIZER_SYS 0

typedef pthread_mutexattr_t   gu_mutexattr_t_SYS;
typedef pthread_mutex_t       gu_mutex_t_SYS;
#define gu_mutex_init_SYS     pthread_mutex_init
#define gu_mutex_lock_SYS     pthread_mutex_lock
#define gu_mutex_unlock_SYS   pthread_mutex_unlock
#define gu_mutex_destroy_SYS  pthread_mutex_destroy

#define GU_MUTEX_INITIALIZER_SYS PTHREAD_MUTEX_INITIALIZER

typedef pthread_condattr_t    gu_condattr_t_SYS;
typedef pthread_cond_t        gu_cond_t_SYS;
#define gu_cond_init_SYS      pthread_cond_init
#define gu_cond_destroy_SYS   pthread_cond_destroy
#define gu_cond_wait_SYS      pthread_cond_wait
#define gu_cond_timedwait_SYS pthread_cond_timedwait
#define gu_cond_signal_SYS    pthread_cond_signal
#define gu_cond_broadcast_SYS pthread_cond_broadcast

#define GU_COND_INITIALIZER_SYS PTHREAD_COND_INITIALIZER

#if defined(__APPLE__) /* emulate barriers missing on MacOS */

#ifdef __cplusplus
extern "C" {
#endif

typedef int gu_barrierattr_t_SYS;
typedef struct
{
    gu_mutex_t_SYS mutex;
    gu_cond_t_SYS  cond;
    int            count;
    int            tripCount;
} gu_barrier_t_SYS;

int gu_barrier_init_SYS   (gu_barrier_t_SYS *barrier,
                           const gu_barrierattr_t_SYS *attr,unsigned int count);
int gu_barrier_destroy_SYS(gu_barrier_t_SYS *barrier);
int gu_barrier_wait_SYS   (gu_barrier_t_SYS *barrier);

#define GU_BARRIER_SERIAL_THREAD_SYS -1

#ifdef __cplusplus
}
#endif

#else  /* native POSIX barriers */

typedef pthread_barrierattr_t  gu_barrierattr_t_SYS;
typedef pthread_barrier_t      gu_barrier_t_SYS;
#define gu_barrier_init_SYS    pthread_barrier_init
#define gu_barrier_destroy_SYS pthread_barrier_destroy
#define gu_barrier_wait_SYS    pthread_barrier_wait

#define GU_BARRIER_SERIAL_THREAD_SYS PTHREAD_BARRIER_SERIAL_THREAD

#endif /* native POSIX barriers */

#endif /* __unix__ */

/**
 *  Depending on compile-time flags application will either use
 *  normal or debug version of the API calls
 */

#ifndef GU_DEBUG_MUTEX
/* GU_DEBUG_MUTEX not defined - use operating system definitions */

typedef gu_mutex_t_SYS gu_mutex_t;

#define gu_mutex_init     gu_mutex_init_SYS
#define gu_mutex_lock     gu_mutex_lock_SYS
#define gu_mutex_unlock   gu_mutex_unlock_SYS
#define gu_mutex_destroy  gu_mutex_destroy_SYS
#define gu_cond_wait      gu_cond_wait_SYS
#define gu_cond_timedwait gu_cond_timedwait_SYS

#define GU_MUTEX_INITIALIZER GU_MUTEX_INITIALIZER_SYS

#else /* GU_DEBUG_MUTEX defined - use custom debug versions of some calls */

typedef struct
{
    gu_mutex_t_SYS  mutex;
    gu_cond_t_SYS   cond;
    gu_thread_t_SYS thread;

    /* point in source code, where called from */
    const char *file;
    int         line;
    int         waiter_count;      //!< # of threads waiting for lock
    int         cond_waiter_count; //!< # of threads waiting for some cond
    bool        locked;            //!< must be 0 or 1
}
gu_mutex_t_DBG;

#define GU_MUTEX_INITIALIZER {              \
        GU_MUTEX_INITIALIZER_SYS,           \
        GU_COND_INITIALIZER_SYS,            \
        GU_THREAD_INITIALIZER_SYS,          \
        __FILE__,                           \
        __LINE__,                           \
        0, 0, false }

#ifdef __cplusplus
extern "C" {
#endif
/** @name Debug versions of basic mutex calls */
/*@{*/
extern
int gu_mutex_init_DBG    (gu_mutex_t_DBG *mutex,
                          const gu_mutexattr_t_SYS *attr,
                          const char *file, unsigned int line);
extern
int gu_mutex_lock_DBG    (gu_mutex_t_DBG *mutex,
                          const char *file, unsigned int line);
extern
int gu_mutex_unlock_DBG  (gu_mutex_t_DBG *mutex,
                          const char *file, unsigned int line);
extern
int gu_mutex_destroy_DBG (gu_mutex_t_DBG *mutex,
                          const char *file, unsigned int line);
extern
int gu_cond_twait_DBG    (gu_cond_t_SYS *cond,
                          gu_mutex_t_DBG *mutex,
                          const struct timespec *abstime,
                          const char *file, unsigned int line);
#ifdef __cplusplus
} // extern "C"
#endif

static inline
int gu_cond_wait_DBG     (gu_cond_t_SYS *cond,
                          gu_mutex_t_DBG *mutex,
                          const char *file, unsigned int line)
{
    return gu_cond_twait_DBG(cond, mutex, NULL, file, line);
}

static inline
bool gu_mutex_locked  (const gu_mutex_t_DBG* m) { return m->locked; }

static inline
bool gu_mutex_owned   (const gu_mutex_t_DBG* m)
{
    return m->locked && gu_thread_equal_SYS(gu_thread_self_SYS(), m->thread);
}

static inline
void gu_mutex_disown (gu_mutex_t_DBG* m)
{
    memset(&m->thread, 0, sizeof(m->thread));
}

/*@}*/

typedef gu_mutex_t_DBG gu_mutex_t;

#define gu_mutex_init(M,A)       gu_mutex_init_DBG   (M,A, __FILE__, __LINE__)
#define gu_mutex_lock(M)         gu_mutex_lock_DBG     (M, __FILE__, __LINE__)
#define gu_mutex_unlock(M)       gu_mutex_unlock_DBG   (M, __FILE__, __LINE__)
#define gu_mutex_destroy(M)      gu_mutex_destroy_DBG  (M, __FILE__, __LINE__)
#define gu_cond_wait(S,M)        gu_cond_wait_DBG    (S,M, __FILE__, __LINE__)
#define gu_cond_timedwait(S,M,T) gu_cond_twait_DBG (S,M,T, __FILE__, __LINE__)

#endif /* DEBUG_MUTEX */

/* declarations without debug variants */
typedef gu_mutexattr_t_SYS gu_mutexattr_t;

typedef gu_thread_t_SYS   gu_thread_t;
#define gu_thread_create  gu_thread_create_SYS
#define gu_thread_join    gu_thread_join_SYS
#define gu_thread_cancel  gu_thread_cancel_SYS
#define gu_thread_exit    gu_thread_exit_SYS
#define gu_thread_detach  gu_thread_detach_SYS
#define gu_thread_self    gu_thread_self_SYS
#define gu_thread_equal   gu_thread_equal_SYS

typedef gu_condattr_t_SYS gu_condattr_t;
typedef gu_cond_t_SYS     gu_cond_t;
#define gu_cond_init      gu_cond_init_SYS
#define gu_cond_destroy   gu_cond_destroy_SYS
#define gu_cond_signal    gu_cond_signal_SYS
#define gu_cond_broadcast gu_cond_broadcast_SYS

#define GU_COND_INITIALIZER GU_COND_INITIALIZER_SYS

typedef gu_barrierattr_t_SYS gu_barrierattr_t;
typedef gu_barrier_t_SYS     gu_barrier_t;
#define gu_barrier_init      gu_barrier_init_SYS
#define gu_barrier_destroy   gu_barrier_destroy_SYS
#define gu_barrier_wait      gu_barrier_wait_SYS

#define GU_BARRIER_SERIAL_THREAD GU_BARRIER_SERIAL_THREAD_SYS

#endif /* _gu_mutex_h_ */
