// Copyright (C) 2007 Codership Oy <info@codership.com>

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
#include "gcs.h" // gcs_to.c functions declared there

struct thread_ctx
{
    pthread_t thread;
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
    x= 2654435761U * x; // http://www.concentric.net/~Ttwang/tech/inthash.htm
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
    // probaboility of conflict 25%, conflict rate 0.375
    register ulong ret = (rnd & 0x70) >> 4;
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

static gcs_to_t*   to          = NULL;
static ulong       thread_max  = 16;    // default number of threads
static gcs_seqno_t seqno_max   = 1<<20; // default number of seqnos to check

/* mutex to synchronize threads start */
static pthread_mutex_t start  = PTHREAD_MUTEX_INITIALIZER;

static const useconds_t t = 10; // optimal sleep time

void* run_thread(void* ctx)
{
    struct thread_ctx* thd = ctx;
    gcs_seqno_t seqno = thd->thread_id; // each thread starts with own offset
                                        // to guarantee uniqueness of seqnos
                           /* GNU libc has them hardware optimized */
#include <endian.h>   // for __BYTE_ORDER et al.
#include <byteswap.h> // for bswap_16(x), bswap_32(x), bswap_64(x) 

/* @note: there are inline functions behind these macros,
 *        so typesafety is taken care of. */

#if   __BYTE_ORDER == __LITTLE_ENDIAN

/* convert to/from Little Endian representation */
#define gu_le16(x) (x)
#define gu_le32(x) (x)
#define gu_le64(x) (x)

/* convert to/from Big Endian representation */
#define gu_be16(x) bswap_16(x) 
#define gu_be32(x) bswap_32(x)
#define gu_be64(x) bswap_64(x)

#elif __BYTE_ORDER == __BIG_ENDIAN

/* convert to/from Little Endian representation */
#define gu_le16(x) bswap_16(x) 
#define gu_le32(x) bswap_32(x)
#define gu_le64(x) bswap_64(x)

/* convert to/from Big Endian representation */
#define gu_be16(x) (x)
#define gu_be32(x) (x)
#define gu_be64(x) (x)

#else

#error "Byte order unrecognized!"

#endif /* __BYTE_ORDER */

/* Analogues to htonl and friends. Since we'll be dealing mostly with
 * little-endian architectures, there is more sense to use little-endian
 * as default */
#define htogs(x) gu_le16(x)
#define gtohs(x) htogs(x)
#define htogl(x) gu_le32(x)
#define gtohl(x) htogl(x)
             // without having to lock mutex
    pthread_mutex_lock   (&start); // wait for start signal
    pthread_mutex_unlock (&start);

    while (seqno < seqno_max) {
        long  ret;
        ulong rnd = my_rnd(seqno);

        if (gu_unlikely(self_cancel(rnd))) {
//            printf("Self-cancelling %8llu\n", seqno);
            while ((ret = gcs_to_self_cancel(to, seqno)) == -EAGAIN) usleep (t);
            if (gu_unlikely(ret)) {
                fprintf (stderr, "gcs_to_self_cancel(%llu) returned %ld (%s)\n",
                         seqno, ret, strerror(-ret));
                exit (EXIT_FAILURE);
            }
            else {
//                printf ("Self-cancel success (%llu)\n", seqno);
                thd->stat_self++;
            }
        }
        else {
//            printf("Grabbing %8llu\n", seqno);
            while ((ret = gcs_to_grab (to, seqno)) == -EAGAIN) usleep (t);
            if (gu_unlikely(ret)) {
                if (gu_likely(-ECANCELED == ret)) {
//                    printf ("canceled (%llu)\n", seqno);
                    thd->stat_fails++;
                }
                else {
                    fprintf (stderr, "gcs_to_grab(%llu) returned %ld (%s)\n",
                             seqno, ret, strerror(-ret));
                    exit (EXIT_FAILURE);
                }
            }
            else {
                long cancels = cancel(rnd);
//                printf ("success (%llu), cancels = %ld\n", seqno, cancels);
                if (gu_likely(cancels)) {
                    long offset = cancel_offset (rnd);
                    gcs_seqno_t cancel_seqno = seqno + offset;

                    while (cancels-- && (cancel_seqno < seqno_max)) {
                        ret = gcs_to_cancel(to, cancel_seqno);
                        if (gu_unlikely(ret)) {
                            fprintf (stderr, "gcs_to_cancel(%llu) by %llu "
                                     "failed: %s\n", cancel_seqno, seqno,
                                     strerror (ret));
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
                ret = gcs_to_release(to, seqno);
            }
        }

        seqno += thread_max; // this together with unique starting point
                             // guarantees that seqnos are unique
    }
//    printf ("Thread %ld exiting. Last seqno = %llu\n",
//            thd->thread_id, seqno - thread_max);
    return NULL;
}

int main (int argc, char* argv[])
{
    // minimum to length required by internal logic
    long to_len = cancel(0xffffffff) * cancel_offset(0xffffffff);

    errno = 0;
    if (argc > 1) seqno_max  = (1 << atol(argv[0]));
    if (argc > 2) thread_max = (1 << atol(argv[1]));
    if (errno) {
        fprintf (stderr, "Usage: %s [seqno [threads]]\nBoth seqno and threads"
                 "are exponents of 2^n.\n", argv[0]);
        exit(errno);
    }
    printf ("Starting with %lu threads and %llu maximum seqno.\n",
            thread_max, seqno_max);

    /* starting with 0, enough space for all threads and cancels */
    to_len = to_len > thread_max ? to_len : thread_max;
    to = gcs_to_create (to_len * 2, 0);

    /* main block */
    {
        long i, ret;
        clock_t start_clock, stop_clock;
        double time_spent;
        struct thread_ctx thread[thread_max];

        pthread_mutex_lock (&start); {
            /* initialize threads */
            for (i = 0; i < thread_max; i++) {
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
        } pthread_mutex_unlock (&start); // release threads

        /* wait for threads to complete and accumulate statistics */
        pthread_join (thread[0].thread, NULL);
        for (i = 1; i < thread_max; i++) {
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
                seqno_max, time_spent, ((double) seqno_max)/time_spent);
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
