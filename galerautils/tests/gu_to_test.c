/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include <stdint.h>
#include <pthread.h>
#include <stdio.h>    // printf()
#include <string.h>   // strerror()
#include <stdlib.h>   // strtol(), exit(), EXIT_SUCCESS, EXIT_FAILURE
#include <errno.h>    // errno
#include <sys/time.h> // gettimeofday()
#include <unistd.h>   // usleep()
#include <check.h>

#include <galerautils.h>

struct thread_ctx
{
    gu_thread_t thread;
    long thread_id;
    long stat_grabs;  // how many times gcs_to_grab() was successful
    long stat_cancels;// how many times gcs_to_cancel() was called
    long stat_fails;  // how many times gcs_to_grab() failed
    long stat_self;   // how many times gcs_self_cancel() was called
};

/* returns a semirandom number (hash) from seqno */
static inline ulong
my_rnd (uint64_t x)
{
    x = 2654435761U * x; // http://www.concentric.net/~Ttwang/tech/inthash.htm
    return (ulong)(x ^ (x >> 32)); // combine upper and lower halfs for better
                                   // randomness
}

/* whether to cancel self */
static inline ulong
self_cancel (ulong rnd)
{
    return !(rnd & 0xf); // will return TRUE once in 16
}

/* how many other seqnos to cancel */
static inline ulong
cancel (ulong rnd)
{
#if 0
    // this causes probablity of conflict 88%
    // and average conflicts per seqno 3.5. Reveals a lot of corner cases
    return (rnd & 0x70) >> 4; // returns 0..7
#else
    // this is more realistic. 
    // probability of conflict 25%, conflict rate 0.375
    ulong ret = (rnd & 0x70) >> 4;
    // returns 0,0,0,0,0,0,1,2
    if (gu_likely(ret < 5)) return 0; else return (ret - 5); 
#endif
}

/* offset of seqnos to cancel */
static inline ulong
cancel_offset (ulong rnd)
{
    return ((rnd & 0x700) >> 8) + 1; // returns 1 - 8
}

static gu_to_t*   to          = NULL;
static ulong      thread_max  = 16;    // default number of threads
static gu_seqno_t seqno_max   = 1<<20; // default number of seqnos to check

/* mutex to synchronize threads start */
static gu_mutex_t start  = GU_MUTEX_INITIALIZER;

static const unsigned int t = 10; // optimal sleep time
static const struct timespec tsleep = { 0, 10000000 }; // 10 ms

void* run_thread(void* ctx)
{
    struct thread_ctx* thd = ctx;
    gu_seqno_t seqno = thd->thread_id; // each thread starts with own offset
                                       // to guarantee uniqueness of seqnos
                                       // without having to lock mutex

    gu_mutex_lock   (&start); // wait for start signal
    gu_mutex_unlock (&start);

    while (seqno < seqno_max) {
        long  ret;
        ulong rnd = my_rnd(seqno);

        if (gu_unlikely(self_cancel(rnd))) {
//            printf("Self-cancelling %8llu\n", (unsigned long long)seqno);
            while ((ret = gu_to_self_cancel(to, seqno)) == -EAGAIN) usleep (t);
            if (gu_unlikely(ret)) {
                fprintf (stderr, "gu_to_self_cancel(%llu) returned %ld (%s)\n",
                         (unsigned long long)seqno, ret, strerror(-ret));
                exit (EXIT_FAILURE);
            }
            else {
//                printf ("Self-cancel success (%llu)\n", (unsigned long long)seqno);
                thd->stat_self++;
            }
        }
        else {
//            printf("Grabbing %8llu\n", (unsigned long long)seqno);
            while ((ret = gu_to_grab (to, seqno)) == -EAGAIN)
                nanosleep (&tsleep, NULL);
            if (gu_unlikely(ret)) {
                if (gu_likely(-ECANCELED == ret)) {
//                    printf ("canceled (%llu)\n", (unsigned long long)seqno);
                    thd->stat_fails++;
                }
                else {
                    fprintf (stderr, "gu_to_grab(%llu) returned %ld (%s)\n",
                             (unsigned long long)seqno, ret, strerror(-ret));
                    exit (EXIT_FAILURE);
                }
            }
            else {
                long cancels = cancel(rnd);
//                printf ("success (%llu), cancels = %ld\n", (unsigned long long)seqno, cancels);
                if (gu_likely(cancels)) {
                    long offset = cancel_offset (rnd);
                    gu_seqno_t cancel_seqno = seqno + offset;

                    while (cancels-- && (cancel_seqno < seqno_max)) {
                        ret = gu_to_cancel(to, cancel_seqno);
                        if (gu_unlikely(ret)) {
                            fprintf (stderr, "gu_to_cancel(%llu) by %llu "
                                     "failed: %s\n",
                                     (unsigned long long)cancel_seqno,
                                     (unsigned long long)seqno,
                                     strerror (-ret));
                            exit (EXIT_FAILURE);
                        }
                        else {
//                            printf ("%llu canceled %llu\n",
//                                    seqno, cancel_seqno);
                            cancel_seqno += offset;
                            thd->stat_cancels++;
                        }
                    }
                }
                thd->stat_grabs++;
                ret = gu_to_release(to, seqno);
                if (gu_unlikely(ret)) {
                    fprintf (stderr, "gu_to_release(%llu) failed: %ld(%s)\n",
                             (unsigned long long)seqno, ret, strerror(-ret));
                    exit (EXIT_FAILURE);
                }
            }
        }

        seqno += thread_max; // this together with unique starting point
                             // guarantees that seqnos are unique
    }
    //    printf ("Thread %ld exiting. Last seqno = %llu\n",
    //        thd->thread_id, (unsigned long long)(seqno - thread_max));
    return NULL;
}

int main (int argc, char* argv[])
{
    // minimum to length required by internal logic
    ulong to_len = cancel(0xffffffff) * cancel_offset(0xffffffff);

    errno = 0;
    if (argc > 1) seqno_max  = (1 << atol(argv[0]));
    if (argc > 2) thread_max = (1 << atol(argv[1]));
    if (errno) {
        fprintf (stderr, "Usage: %s [seqno [threads]]\nBoth seqno and threads"
                 "are exponents of 2^n.\n", argv[0]);
        exit(errno);
    }
    printf ("Starting with %lu threads and %llu maximum seqno.\n",
            thread_max, (unsigned long long)seqno_max);

    /* starting with 0, enough space for all threads and cancels */
    // 4 is a magic number to get it working without excessive sleep on amd64
    to_len = to_len > thread_max ? to_len : thread_max; to_len *= 4;
    to = gu_to_create (to_len, 0);
    if (to != NULL) {
        printf ("Created TO monitor of length %lu\n", to_len);
    }
    else {
        exit (-ENOMEM);
    }

    /* main block */
    {
        long i, ret;
        clock_t start_clock, stop_clock;
        double time_spent;
        struct thread_ctx thread[thread_max];

        gu_mutex_lock (&start); {
            /* initialize threads */
            for (i = 0; (ulong)i < thread_max; i++) {
                thread[i].thread_id    = i;
                thread[i].stat_grabs   = 0;
                thread[i].stat_cancels = 0;
                thread[i].stat_fails   = 0;
                thread[i].stat_self    = 0;
                ret = pthread_create(&(thread[i].thread), NULL, run_thread,
                                     &thread[i]);
                if (ret) {
                    fprintf (stderr, "Failed to create thread %ld: %s",
                             i, strerror(ret));
                    exit (EXIT_FAILURE);
                }
            }
            start_clock = clock();
        } gu_mutex_unlock (&start); // release threads

        /* wait for threads to complete and accumulate statistics */
        gu_thread_join (thread[0].thread, NULL);
        for (i = 1; (ulong)i < thread_max; i++) {
            pthread_join (thread[i].thread, NULL);
            thread[0].stat_grabs   += thread[i].stat_grabs;
            thread[0].stat_cancels += thread[i].stat_cancels;
            thread[0].stat_fails   += thread[i].stat_fails;
            thread[0].stat_self    += thread[i].stat_self;
        }
        stop_clock = clock();
        time_spent = gu_clock_diff (stop_clock,start_clock);

        /* print statistics */
        printf ("%llu seqnos in %.3f seconds (%.3f seqno/sec)\n",
                (unsigned long long)seqno_max, time_spent,
                ((double) seqno_max)/time_spent);
        printf ("Overhead at 10000 actions/second: %.2f%%\n",
                (time_spent * 10000 * 100/* for % */)/seqno_max);
        printf ("Grabbed:        %9lu\n"
                "Failed:         %9lu\n"
                "Self-cancelled: %9lu\n"
                "Canceled:       %9lu (can exceed total number of seqnos)\n",
                thread[0].stat_grabs,   thread[0].stat_fails,
                thread[0].stat_self, thread[0].stat_cancels
            );
        if (seqno_max !=
            (thread[0].stat_grabs+thread[0].stat_fails+thread[0].stat_self)) {
            fprintf (stderr, "Error: total number of grabbed, failed and "
                     "self-cancelled waiters does not match total seqnos.\n");
            exit (EXIT_FAILURE);
        }
    }
    return 0;
}
