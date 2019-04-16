/*
 * Copyright (C) 2010-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file GCS Send Monitor. To ensure fair (FIFO) access to gcs_core_send()
 */

#ifndef _gcs_sm_h_
#define _gcs_sm_h_

#include "gu_datetime.hpp"
#include <galerautils.h>
#include <errno.h>

#ifdef GCS_SM_CONCURRENCY
#define GCS_SM_CC sm->cc
#else
#define GCS_SM_CC 1
#endif /* GCS_SM_CONCURRENCY */

typedef struct gcs_sm_user
{
    gu_cond_t* cond;
    bool       wait;
}
gcs_sm_user_t;

typedef struct gcs_sm_stats
{
    long long sample_start;// beginning of the sample period
    long long pause_start; // start of the pause
    long long paused_ns;     // total nanoseconds paused
    long long paused_sample; // paused_ns at the beginning of the sample
    long long send_q_samples;
    long long send_q_len;
    long long send_q_len_max;
    long long send_q_len_min;
}
gcs_sm_stats_t;

typedef struct gcs_sm
{
    gcs_sm_stats_t stats;
    gu_mutex_t    lock;
    gu_cond_t     cond;
    long          cond_wait;
    unsigned long wait_q_len;
    unsigned long wait_q_mask;
    unsigned long wait_q_head;
    unsigned long wait_q_tail;
    long          users;
    long          users_min;
    long          users_max;
    long          entered;
    long          ret;
#ifdef GCS_SM_CONCURRENCY
    long          cc;
#endif /* GCS_SM_CONCURRENCY */
    bool          pause;
    gu::datetime::Period wait_time;

#ifdef GCS_SM_DEBUG
#define GCS_SM_HIST_STR_LEN 128
#define GCS_SM_HIST_LEN     1024
    char history[GCS_SM_HIST_LEN][GCS_SM_HIST_STR_LEN];
    int  history_line;
#endif /* GCS_SM_DEBUG */

    gcs_sm_user_t wait_q[];
}
gcs_sm_t;

#ifdef GCS_SM_DEBUG
#define GCS_SM_HIST_LOG(fmt, ...)                                       \
    {                                                                   \
        sm->history_line = (sm->history_line + 1) % GCS_SM_HIST_LEN;    \
        char* const line(sm->history[sm->history_line]);                \
        snprintf(line, GCS_SM_HIST_STR_LEN - 1,                         \
                 "%8lx|%s(h:%lu,t:%lu,u:%ld,e:%ld,p:%d,r:%ld):%d: " fmt "\n", \
                 gu_thread_self(), __func__,                            \
                 sm->wait_q_head, sm->wait_q_tail, sm->users, sm->entered, \
                 sm->pause, sm->ret, __LINE__, ##__VA_ARGS__);          \
    }

/*!
 * Dumps SM state and history to file
 */
extern void
_gcs_sm_dump_state_common(gcs_sm_t* sm, FILE* file); // unprotected

#define GCS_SM_ASSERT(expr)                              \
    if (!(expr)) {                                       \
        GCS_SM_HIST_LOG("assertion %s failed\n", #expr); \
        _gcs_sm_dump_state_common(sm, stderr);           \
        assert(expr);                                    \
    }

extern void
gcs_sm_dump_state(gcs_sm_t* sm, FILE* file);

#else
#define GCS_SM_HIST_LOG(fmt, ...) {}
#define GCS_SM_ASSERT(expr) assert(expr);
#endif /* GCS_SM_DEBUG */

/*!
 * Creates send monitor
 *
 * @param len size of the monitor, should be a power of 2
 * @param n   concurrency parameter (how many users can enter at the same time)
 */
extern gcs_sm_t*
gcs_sm_create (long len, long n);

/*!
 * Closes monitor for entering and makes all users to exit with error.
 * (entered users are not affected). Blocks until everybody exits
 */
extern long
gcs_sm_close (gcs_sm_t* sm);

/*!
 * (Re)opens monitor for entering.
 */
extern long
gcs_sm_open (gcs_sm_t* sm);

/*!
 * Deallocates resources associated with the monitor
 */
extern void
gcs_sm_destroy (gcs_sm_t* sm);

#define GCS_SM_INCREMENT(cursor) (cursor = ((cursor + 1) & sm->wait_q_mask))

static inline void
_gcs_sm_wake_up_next (gcs_sm_t* sm)
{
    long woken = sm->entered;

    assert (woken >= 0);
    assert (woken <= GCS_SM_CC);

    while (woken < GCS_SM_CC && sm->users > 0) {
        if (gu_likely(sm->wait_q[sm->wait_q_head].wait)) {
            assert (NULL != sm->wait_q[sm->wait_q_head].cond);
            // gu_debug ("Waking up %lu", sm->wait_q_head);
            gu_cond_signal (sm->wait_q[sm->wait_q_head].cond);
            woken++;
            GCS_SM_HIST_LOG("signaled %lu", sm->wait_q_head);
            break;
        }
        else { /* skip interrupted */
            assert (NULL == sm->wait_q[sm->wait_q_head].cond);
            gu_debug ("Skipping interrupted: %lu", sm->wait_q_head);
            sm->users--;
            if (gu_unlikely(sm->users < sm->users_min)) {
                sm->users_min = sm->users;
            }
            GCS_SM_HIST_LOG("skipped %lu", sm->wait_q_head);
            GCS_SM_INCREMENT(sm->wait_q_head);
        }
    }

    assert (woken <= GCS_SM_CC);
    assert (sm->users >= 0);
}

/* wake up whoever might be waiting there */
static inline void
_gcs_sm_wake_up_waiters (gcs_sm_t* sm)
{
    if (gu_unlikely(sm->cond_wait)) {
        assert (sm->cond_wait > 0);
        sm->cond_wait--;
        GCS_SM_HIST_LOG("signal global cond");
        gu_cond_signal (&sm->cond);
    }
    else if (!sm->pause) {
        _gcs_sm_wake_up_next(sm);
    }
    else {
        /* gcs_sm_continue() will do the rest */
        GCS_SM_HIST_LOG("skipped wake up waiters");
    }
}

static inline void
_gcs_sm_leave_common (gcs_sm_t* sm)
{
    GCS_SM_ASSERT(sm->users > 0);
    sm->users--;

    if (gu_unlikely(sm->users < sm->users_min)) {
        sm->users_min = sm->users;
    }

    GCS_SM_ASSERT(false == sm->wait_q[sm->wait_q_head].wait);
    GCS_SM_ASSERT(NULL  == sm->wait_q[sm->wait_q_head].cond);
    GCS_SM_INCREMENT(sm->wait_q_head);
    _gcs_sm_wake_up_waiters (sm);

    GCS_SM_HIST_LOG("leaving");
}

//#define GCS_SM_SIMULATE_TIMEOUTS

static inline int
_gcs_sm_enqueue_common (gcs_sm_t* sm, gu_cond_t* cond, bool block,
                        unsigned long tail)
{
    sm->wait_q[tail].cond = cond;
    sm->wait_q[tail].wait = true;
    int ret;

    if (block == true)
    {
        GCS_SM_HIST_LOG("queueing at %lu", tail);
        gu_cond_wait (cond, &sm->lock);
        assert(tail == sm->wait_q_head || false == sm->wait_q[tail].wait);
        assert(sm->wait_q[tail].cond == cond || false == sm->wait_q[tail].wait);
        ret = sm->wait_q[tail].wait ? 0 : -EINTR;
    }
    else
    {
        gu::datetime::Date abstime(gu::datetime::Date::calendar());
#ifdef GCS_SM_SIMULATE_TIMEOUTS
        if (tail & 1)
#endif
        abstime = abstime + sm->wait_time;
        struct timespec ts;
        abstime._timespec(ts);
        GCS_SM_HIST_LOG("waiting at %lu", tail);
        ret = -gu_cond_timedwait(cond, &sm->lock, &ts);
        if (0 == ret)
        {
            ret = sm->wait_q[tail].wait ? 0 : -EINTR;
            // sm->wait_time is incremented by second each time cond wait
            // times out, reset back to one second when cond wait succeeds.
            sm->wait_time = std::max(sm->wait_time*2/3,
                                     gu::datetime::Period(gu::datetime::Sec));
        }
        else if (-ETIMEDOUT == ret)
        {
            if (sm->wait_time < 10 * gu::datetime::Sec)
            {
                gu_debug("send monitor wait timed out, waited for %s",
                         to_string(sm->wait_time).c_str());
            }
            else
            {
                gu_warn("send monitor wait timed out, waited for %s",
                        to_string(sm->wait_time).c_str());
            }
#ifndef GCS_SM_SIMULATE_TIMEOUTS
            if (tail & 1)
#endif
            sm->wait_time = sm->wait_time + gu::datetime::Sec;
        }
        else
        {
            gu_error("send monitor timedwait failed with %d: %s",
                     ret, strerror(-ret));
        }

        // to reproduce GAL-495: if (0 == ret && (tail & 1)) { ret = -EINTR; }
    }

    sm->wait_q[tail].cond = NULL;
    sm->wait_q[tail].wait = false;

    if (gu_unlikely(0 != ret)) GCS_SM_HIST_LOG("%ld wait failed: %d", tail, ret);

    return ret;
}

#ifdef GCS_SM_CONCURRENCY
#define GCS_SM_HAS_TO_WAIT                                              \
    (sm->users > (sm->entered + 1) || sm->entered >= GCS_SM_CC || sm->pause)
#else
#define GCS_SM_HAS_TO_WAIT (sm->users > 1 || sm->entered >= GCS_SM_CC || sm->pause)
#endif /* GCS_SM_CONCURRENCY */

/*!
 * Synchronize with entry order to the monitor. Must be always followed by
 * gcs_sm_enter(sm, cond, true)
 *
 * @retval -EAGAIN - out of space
 * @retval -EBADFD - monitor closed
 * @retval >= 0 queue handle
 */
static inline long
gcs_sm_schedule (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    long ret = sm->ret;

    if (gu_likely((sm->users < (long)sm->wait_q_len) && (0 == ret))) {

        sm->users++;
        if (gu_unlikely(sm->users > sm->users_max)) {
            sm->users_max = sm->users;
        }
        GCS_SM_INCREMENT(sm->wait_q_tail); /* even if we don't queue, cursor
                                            * needs to be advanced */
        sm->stats.send_q_samples++;

        if (GCS_SM_HAS_TO_WAIT) {
            ret = sm->wait_q_tail + 1; // waiter handle

            /* here we want to distinguish between FC pause and real queue */
            sm->stats.send_q_len += sm->users - 1;
        }

        GCS_SM_HIST_LOG("scheduled at %lu", sm->wait_q_tail);

        return ret; // success
    }
    else if (0 == ret) {
        assert (sm->users == (long)sm->wait_q_len);
        ret = -EAGAIN;
    }

    assert(ret < 0);

    GCS_SM_HIST_LOG("return %ld", sm->wait_q_tail);
    gu_mutex_unlock (&sm->lock);

    return ret;
}

/*!
 * Enter send monitor critical section
 *
 * @param sm   send monitor object
 * @param cond condition to signal to wake up thread in case of wait
 * @param block if true block until entered or send monitor is closed,
 *              if false enter wait times out eventually
 *
 * @retval -EAGAIN - out of space
 * @retval -EBADFD - monitor closed
 * @retval -EINTR  - was interrupted by another thread
 * @retval -ETIMEDOUT - timedout waiting for its turn
 * @retval 0 - successfully entered
 */
static inline long
gcs_sm_enter (gcs_sm_t* sm, gu_cond_t* cond, bool scheduled, bool block)
{
    long ret = 0; /* if scheduled and no queue */

    if (gu_likely (scheduled || (ret = gcs_sm_schedule(sm)) >= 0)) {
        const unsigned long tail(sm->wait_q_tail);

        /* we want to enqueue at least once, if gcs_sm_schedule()
           did create a waiter handle (i.e. if GCS_SM_HAS_TO_WAIT
           was true) */
        bool wait = GCS_SM_HAS_TO_WAIT;
        while (wait && ret >= 0) {
            ret = _gcs_sm_enqueue_common (sm, cond, block, tail);
            if (gu_likely((0 == ret))) {
                ret = sm->ret;
                /* weaken the condition, so that we do enter if there
                   is room for one more thread */
                wait = sm->entered >= GCS_SM_CC;
            }
        }

        assert (ret <= 0);

        if (gu_likely(0 == ret)) {
            assert(sm->users   > 0);
            assert(sm->entered < GCS_SM_CC);
            sm->entered++;
#ifdef GCS_SM_SIMULATE_TIMEOUTS
            if (tail & 1) usleep(1000);
#endif
        }
        else {
            if (tail != sm->wait_q_head) {
                /* was interrupted in the middle,
                 * will be handled by someone else (with tail == head) */
            }
            else {
                GCS_SM_ASSERT(-EINTR != ret || sm->pause);
                /* update head, wake up next */
                _gcs_sm_leave_common(sm);
            }
        }

        GCS_SM_HIST_LOG("%lu entered: %ld", tail, ret);
        gu_mutex_unlock (&sm->lock);
    }
    else if (ret != -EBADFD){
        gu_warn("thread %ld failed to schedule for monitor: %ld (%s)",
                gu_thread_self(), ret, strerror(-ret));
    }

    return ret;
}

static inline void
gcs_sm_leave (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    GCS_SM_ASSERT(sm->entered > 0);
    sm->entered--;
    GCS_SM_ASSERT(sm->entered < GCS_SM_CC);

    _gcs_sm_leave_common(sm);

    gu_mutex_unlock (&sm->lock);
}

static inline void
gcs_sm_pause (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    /* don't pause closed monitor */
    if (gu_likely(0 == sm->ret) && !sm->pause) {
        sm->stats.pause_start = gu_time_monotonic();
        sm->pause = true;
    }
    GCS_SM_HIST_LOG("paused");
    gu_mutex_unlock (&sm->lock);
}

static inline void
_gcs_sm_continue_common (gcs_sm_t* sm)
{
    sm->pause = false;

    _gcs_sm_wake_up_next(sm); /* wake up next waiter if any */
}

static inline void
gcs_sm_continue (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    if (gu_likely(sm->pause)) {
        _gcs_sm_continue_common (sm);

        sm->stats.paused_ns += gu_time_monotonic() - sm->stats.pause_start;
    }
    else {
        gu_debug("Trying to continue unpaused monitor");
    }
    GCS_SM_HIST_LOG("resumed");
    gu_mutex_unlock (&sm->lock);
}

/*!
 * Interrupts waiter identified by handle (returned by gcs_sm_schedule())
 *
 * @retval 0 - success
 * @retval -ESRCH - waiter is not in the queue. For practical purposes
 *                  it is impossible to discern already interrupted waiter and
 *                  the waiter that has entered the monitor
 */
static inline long
gcs_sm_interrupt (gcs_sm_t* sm, long handle)
{
    assert (handle > 0);
    long ret;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    handle--;

    if (gu_likely(sm->wait_q[handle].wait)) {
        assert (sm->wait_q[handle].cond != NULL);
        sm->wait_q[handle].wait = false;
        gu_cond_signal (sm->wait_q[handle].cond);
        GCS_SM_HIST_LOG("interrupted %ld", handle);
        sm->wait_q[handle].cond = NULL;
        ret = 0;
        if (!sm->pause && handle == (long)sm->wait_q_head) {
            /* gcs_sm_interrupt() was called right after the waiter was
             * signaled by gcs_sm_continue() or gcs_sm_leave() but before
             * the waiter has woken up. Wake up the next waiter */
            _gcs_sm_wake_up_next(sm);
        }
    }
    else {
        ret = -ESRCH;
        GCS_SM_HIST_LOG("interrupted %ld: not found", handle);
    }

    gu_mutex_unlock (&sm->lock);

    return ret;
}

/*!
 * Each call to this function resets stats and starts new sampling interval
 *
 * @param q_len      current send queue length
 * @param q_len_avg  set to an average number of preceding users seen by each
 *                   new one (not including itself) (-1 if stats overflown)
 * @param q_len_max  maximum send queue length since last call
 * @param q_len_min  minimum send queue length since last call
 * @param paused_ns  total time paused (nanoseconds)
 * @param paused_avg set to a fraction of time which monitor spent in a paused
 *                   state (-1 if stats overflown)
 */
extern void
gcs_sm_stats_get (gcs_sm_t*  sm,
                  int*       q_len,
                  int*       q_len_max,
                  int*       q_len_min,
                  double*    q_len_avg,
                  long long* paused_ns,
                  double*    paused_avg);

/*! resets average/max/min stats calculation */
extern void
gcs_sm_stats_flush(gcs_sm_t* sm);

/*! Grabs sm object for out-of-order access
 * @return 0 or negative error code */
static inline long
gcs_sm_grab (gcs_sm_t* sm)
{
    long ret;

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    while (!(ret = sm->ret) && sm->entered >= GCS_SM_CC) {
        sm->cond_wait++;
        gu_cond_wait (&sm->cond, &sm->lock);
    }

    if (ret) {
        assert (ret < 0);
        GCS_SM_HIST_LOG("grab failed");
        _gcs_sm_wake_up_waiters (sm);
    }
    else {
        assert (sm->entered < GCS_SM_CC);
        sm->entered++;
        GCS_SM_HIST_LOG("grab succeeded");
    }

    gu_mutex_unlock (&sm->lock);

    return ret;
}

/*! Releases sm object after gcs_sm_grab() */
static inline void
gcs_sm_release (gcs_sm_t* sm)
{
    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->entered--;
    assert(sm->entered >= 0);
    _gcs_sm_wake_up_waiters (sm);
    GCS_SM_HIST_LOG("released");

    gu_mutex_unlock (&sm->lock);
}

#endif /* _gcs_sm_h_ */
