// Copyright (C) 2008 Codership Oy <info@codership.com>

/**
 * @file time manipulation functions/macros
 *
 * $Id$
 */

#ifndef _gu_time_h_
#define _gu_time_h_

#include <sys/time.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Returns seconds */
static inline double
gu_timeval_diff (struct timeval* left, struct timeval* right)
{
    long long diff = left->tv_sec;
    diff = ((diff - right->tv_sec)*1000000LL) + left->tv_usec - right->tv_usec;
    return (((double)diff) * 1.0e-06);
}

static inline void
gu_timeval_add (struct timeval* t, double s)
{
    double ret = (double)t->tv_sec + ((double)t->tv_usec) * 1.0e-06 + s;

    t->tv_sec  = (long)ret;
    t->tv_usec = (long)((ret - (double)t->tv_sec) * 1.0e+06);
}

static const double SEC_PER_CLOCK = ((double)1.0)/CLOCKS_PER_SEC;

/** Returns seconds */
static inline double
gu_clock_diff (clock_t left, clock_t right)
{
    return ((double)(left - right)) * SEC_PER_CLOCK;
}

#include <unistd.h>

/**
 * New time interface
 *
 * All funcitons return nanoseconds.
 */

/* Maximum date representable by long long and compatible with timespec */
#define GU_TIME_ETERNITY 9223372035999999999LL

#if defined(__APPLE__) /* synced with linux/time.h */
# define CLOCK_REALTIME  0
# define CLOCK_MONOTONIC 1
typedef int clockid_t;
int clock_gettime (clockid_t clk_id, struct timespec * tp);
#endif /* __APPLE__ */

static inline long long
gu_time_getres()
{
#if _POSIX_TIMERS > 0
    struct timespec tmp;
    clock_getres (CLOCK_REALTIME, &tmp);
    return ((tmp.tv_sec * 1000000000LL) + tmp.tv_nsec);
#else
    return 1000LL; // assumed resolution of gettimeofday() in nanoseconds
#endif
}

static inline long long
gu_time_calendar()
{
#if _POSIX_TIMERS > 0 || defined(__APPLE__)
    struct timespec tmp;
    clock_gettime (CLOCK_REALTIME, &tmp);
    return ((tmp.tv_sec * 1000000000LL) + tmp.tv_nsec);
#else
    struct timeval tmp;
    gettimeofday (&tmp, NULL);
    return ((tmp.tv_sec * 1000000000LL) + (tmp.tv_usec * 1000LL));
#endif
}

static inline long long
gu_time_monotonic()
{
#if defined(_POSIX_MONOTONIC_CLOCK) || defined(__APPLE__)
    struct timespec tmp;
    clock_gettime (CLOCK_MONOTONIC, &tmp);
    return ((tmp.tv_sec * 1000000000LL) + tmp.tv_nsec);
#else
    struct timeval tmp;
    gettimeofday (&tmp, NULL);
    return ((tmp.tv_sec * 1000000000LL) + (tmp.tv_usec * 1000LL));
#endif
}

#ifdef CLOCK_PROCESS_CPUTIME_ID
static inline long long
gu_time_process_cputime()
{
#if _POSIX_TIMERS > 0
    struct timespec tmp;
    clock_gettime (CLOCK_PROCESS_CPUTIME_ID, &tmp);
    return ((tmp.tv_sec * 1000000000LL) + tmp.tv_nsec);
#else
    return -1;
#endif
}
#endif /* CLOCK_PROCESS_CPUTIME_ID */


static inline long long
gu_time_thread_cputime()
{
#if _POSIX_TIMERS > 0
    struct timespec tmp;
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &tmp);
    return ((tmp.tv_sec * 1000000000LL) + tmp.tv_nsec);
#else
    return -1;
#endif
}

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* _gu_time_h_ */
