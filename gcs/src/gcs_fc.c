/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! @file This unit contains Flow Control parts deemed worthy to be 
 *        taken out of gcs.c */

#include "gcs_fc.h"

#include <galerautils.h>
#include <string.h>

double const gcs_fc_hard_limit_fix = 0.9; //! allow for some overhead

static double const min_sleep = 0.001; //! minimum sleep period (s)

/*! Initializes operational constants before opening connection to group
 * @return -EINVAL if wrong values are submitted */
int
gcs_fc_init (gcs_fc_t* fc,
             ssize_t   hard_limit,   // slave queue hard limit
             double    soft_limit,   // soft limit as a fraction of hard limit
             double    max_throttle)
{
    assert (fc);

    if (hard_limit < 0) {
        gu_error ("Bad value for slave queue hard limit: %zd (should be > 0)",
                  hard_limit);
        return -EINVAL;
    }

    if (soft_limit < 0.0 || soft_limit >= 1.0) {
        gu_error ("Bad value for slave queue soft limit: %f "
                  "(should belong to [0.0,1.0) )", soft_limit);
        return -EINVAL;
    }

    if (max_throttle < 0.0 || max_throttle >= 1.0) {
        gu_error ("Bad value for max throttle: %f "
                  "(should belong to [0.0,1.0) )", max_throttle);
        return -EINVAL;
    }

    memset (fc, 0, sizeof(*fc));

    fc->hard_limit = hard_limit;
    fc->soft_limit = fc->hard_limit * soft_limit;
    fc->max_throttle = max_throttle;

    return 0;
}

/*! Reinitializes object at the beginning of state transfer */
void
gcs_fc_reset (gcs_fc_t* const fc, ssize_t const queue_size)
{
    assert (fc != NULL);
    assert (queue_size >= 0);

    fc->init_size  = queue_size;
    fc->size       = fc->init_size;
    fc->start      = gu_time_monotonic();
    fc->last_sleep = 0;
    fc->act_count  = 0;
    fc->max_rate   = -1.0;
    fc->scale      =  0.0;
    fc->offset     =  0.0;
    fc->sleep_count= 0;
    fc->sleeps     = 0.0;
}

/*
 * The idea here is that there is no flow control up until slave queue size
 * reaches soft limit.
 * After that flow control gradually slows down replication rate by emitting FC
 * events in order to buy more time for state transfer.
 * Replication rate goes linearly from normal rate at soft limit to max_throttle
 * fraction at hard limit, at which point -ENOMEM is returned as replication
 * becomes prohibitively slow.
 *
 * replication
 *    speed
 *      ^
 *      |--------.           <- normal replication rate
 *      |        .\
 *      |        . \
 *      |        .  \
 *      |        .   \      speed = fc->size * fc->scale + fc->offset
 *      |        .    \
 *      |        .     \
 *      |        .      \  |
 *      |        .       \ |
 *      |        .        \|
 *      |        .         + <- throttle limit
 *      |        .         |
 *      |        .         |
 *      +--------+---------+----> slave queue size
 *             soft       hard
 *            limit       limit
 */

/*! Processes a new action added to a slave queue.
 *  @return length of sleep in nanoseconds or negative error code
 *          or GU_TIME_ETERNITY for complete stop */
long long
gcs_fc_process (gcs_fc_t* fc, ssize_t act_size)
{
    fc->size += act_size;
    fc->act_count++;

    if (fc->size <= fc->soft_limit) {
        /* normal operation */
        if (gu_unlikely(fc->debug > 0 && !(fc->act_count % fc->debug))) {
            gu_info ("FC: queue size: %zdb (%4.1f%% of soft limit)",
                     fc->size, ((double)fc->size)/fc->soft_limit*100.0);
        }
        return 0;
    }
    else if (fc->size >= fc->hard_limit) {
        if (0.0 == fc->max_throttle) {
            /* we can accept total service outage */
            return GU_TIME_ETERNITY;
        }
        else {
            gu_error ("Recv queue hard limit exceded. Can't continue.");
            return -ENOMEM;
        }
    }
//    else if (!(fc->act_count & 7)) { // do this for every 8th action
    else {
        long long end   = gu_time_monotonic();
        double interval = ((end - fc->start) * 1.0e-9);

        if (gu_unlikely (0 == fc->last_sleep)) {
            /* just tripped the soft limit, preparing constants for throttle */

            fc->max_rate = (double)(fc->size - fc->init_size) / interval;

            double s = (1.0 - fc->max_throttle)/(fc->soft_limit-fc->hard_limit);
            assert (s < 0.0);

            fc->scale  = s * fc->max_rate;
            fc->offset = (1.0 - s*fc->soft_limit) * fc->max_rate;

            // calculate time interval from the soft limit
            interval = interval * (double)(fc->size - fc->soft_limit) /
                (fc->size - fc->init_size);
            assert (interval >= 0.0);

            // Move reference point to soft limit
            fc->last_sleep = fc->soft_limit;
            fc->start      = end - interval;

            gu_warn("Soft recv queue limit exceeded, starting replication "
                    "throttle. Measured avg. rate: %f bytes/sec; "
                    "Throttle parameters: scale=%f, offset=%f",
                    fc->max_rate, fc->scale, fc->offset);
        }

        /* throttling operation */
        double desired_rate = fc->size * fc->scale + fc->offset; // linear decay
        //double desired_rate = fc->max_rate * fc->max_throttle; // square wave
        assert (desired_rate <= fc->max_rate);

        double sleep = (double)(fc->size - fc->last_sleep) / desired_rate
            - interval;

        if (gu_unlikely(fc->debug > 0 && !(fc->act_count % fc->debug))) {
            gu_info ("FC: queue size: %zdb, length: %zd, "
                     "measured rate: %fb/s, desired rate: %fb/s, "
                     "interval: %5.3fs, sleep: %5.4fs. "
                     "Sleeps initiated: %zd, for a total of %6.3fs",
                     fc->size, fc->act_count,
                     ((double)(fc->size - fc->last_sleep))/interval,
                     desired_rate, interval, sleep, fc->sleep_count,
                     fc->sleeps);
            fc->sleep_count = 0;
            fc->sleeps = 0.0;
        }

        if (gu_likely(sleep < min_sleep)) {
#if 0
            gu_info ("Skipping sleep: desired_rate = %f, sleep = %f (%f), "
                     "interval = %f, fc->scale = %f, fc->offset = %f, "
                     "fc->size = %zd",
                     desired_rate, sleep, min_sleep, interval,
                     fc->scale, fc->offset, fc->size);
#endif
            return 0;
        }

        fc->last_sleep = fc->size;
        fc->start      = end;
        fc->sleep_count++;
        fc->sleeps += sleep;

        return (1000000000LL * sleep);
    }

    return 0;
}

void gcs_fc_debug (gcs_fc_t* fc, long debug_level) { fc->debug = debug_level; }
