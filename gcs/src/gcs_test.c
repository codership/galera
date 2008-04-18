/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */
/***********************************************************/
/*  This program imitates 3rd party application and        */
/*  tests GCS library in a dummy standalone configuration  */
/***********************************************************/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "gcs.h"
#include "gcs_test.h"

#define gcs_malloc(a) ((a*) malloc (sizeof (a)))

static pthread_mutex_t gcs_test_lock = PTHREAD_MUTEX_INITIALIZER; 

typedef struct gcs_test_log
{
    FILE *file;
    pthread_mutex_t lock;
}
gcs_test_log_t;

#define SEND_LOG "/dev/shm/gcs_test_send.log"
#define RECV_LOG "/dev/shm/gcs_test_recv.log"

static gcs_test_log_t *send_log, *recv_log;

static bool throughput = true;  // bech for throughput
static bool total      = false; // also inact TO locking

typedef enum
{
    GCS_TEST_SEND,
    GCS_TEST_RECV,
    GCS_TEST_REPL
}
gcs_test_repl_t;

typedef struct gcs_test_thread
{
    pthread_t       thread;
    long            id;
    gcs_test_repl_t type;
    gcs_seqno_t     act_id;
    gcs_seqno_t     local_act_id;
    gcs_act_type_t  act_type;
    long            n_tries;
    size_t          msg_len;
    char           *msg;
    char           *log_msg;
}
gcs_test_thread_t;

#define MAX_MSG_LEN 1 << 16

static long gcs_test_thread_create (gcs_test_thread_t *t, long id, long n_tries)
{
    t->id           = id;
    t->act_id       = GCS_SEQNO_ILL;
    t->local_act_id = GCS_SEQNO_ILL;
    t->act_type     = GCS_ACT_DATA;
    t->n_tries      = n_tries;
    t->msg_len      = MAX_MSG_LEN;
    t->msg = (char *) malloc (MAX_MSG_LEN * sizeof(char));
    if (t->msg)
    {
	t->log_msg = (char *) malloc (MAX_MSG_LEN * sizeof(char));
	if (t->log_msg) return 0;
    }

    return errno;
}

static long gcs_test_thread_destroy (gcs_test_thread_t *t)
{
    if (t->msg)     free (t->msg);
    if (t->log_msg) free (t->log_msg);
    return 0;
}

typedef struct gcs_test_thread_pool
{
    long              n_threads;
    long              n_tries;
    long              n_started;
    gcs_test_repl_t   type;
    gcs_test_thread_t *threads;
}
gcs_test_thread_pool_t;

static long gcs_test_thread_pool_create (gcs_test_thread_pool_t *pool,
					const gcs_test_repl_t   type,
					const long              n_threads,
					const long              n_tries)
{
    long err = 0;
    long i;

//    pool = gcs_malloc (gcs_test_thread_pool_t);
//    if (!pool) { err = errno; goto out; }

    pool->n_threads = n_threads;
    pool->type      = type;
    pool->n_tries   = n_tries;
    pool->n_started = 0;

    pool->threads = (gcs_test_thread_t *) calloc
	(pool->n_threads, sizeof (gcs_test_thread_t));
    if (!pool->threads)
    {
	error (0, errno,
	       "Failed to allocate %ld thread objects", n_threads);
	err = errno; 
	goto out1;
    }

    for (i = 0; i < pool->n_threads; i++)
    {
	if ((err = gcs_test_thread_create (pool->threads + i, i, n_tries)))
	{
	    error (0, errno,
		   "Failed to create thread object %ld", i);
	    err = errno;
	    goto out2;
	}
    }
//    printf ("Created %ld thread objects\n", i);
    return 0;

out2:
    while (i)
    {
	i--;
	gcs_test_thread_destroy (pool->threads + i);
    }
    free (pool->threads);
out1:
    free (pool);
//out:
    return err;
}

static void
gcs_test_thread_pool_destroy (gcs_test_thread_pool_t* pool)
{
    long i;
    if (pool->threads) {
        for (i = 0; i < pool->n_threads; i++) {
            gcs_test_thread_destroy (pool->threads + i);
        }
        free (pool->threads);
    }
}

static pthread_mutex_t make_msg_lock = PTHREAD_MUTEX_INITIALIZER;
//static long total_tries;

static inline long
test_make_msg (char* msg, const long mlen)
{
    static gcs_seqno_t count = 1;
    size_t len = 0;
    gcs_seqno_t tmp;

    if (!throughput) {
        pthread_mutex_lock   (&make_msg_lock);
        tmp = count++;
        pthread_mutex_unlock (&make_msg_lock);

        len = snprintf (msg, mlen, "%10d %9llu %s",
                        rand(), (unsigned long long)count++, gcs_test_data);
    }
    else {
        len = rand() % mlen + 1; // just random length, we don't care about
                                 // contents
    }

    if (len >= mlen)
        return mlen;
    else
        return len;
}

static long
test_log_open (gcs_test_log_t **log, const char *name)
{
    char real_name[1024];
    gcs_test_log_t *l = gcs_malloc (gcs_test_log_t);

    if (!l) return errno;
    
    snprintf (real_name, 1024, "%s.%d", name, getpid());
    if (!(l->file = fopen (real_name, "w"))) return errno;
    pthread_mutex_init (&l->lock, NULL);
    *log = l;
    return 0;
}

static long
test_log_close (gcs_test_log_t **log)
{
    long err = 0;
    gcs_test_log_t *l = *log;
    
    if (l)
    {
	pthread_mutex_lock (&l->lock);
	err = fclose (l->file);
	pthread_mutex_unlock (&l->lock);
	pthread_mutex_destroy (&l->lock);
    }
    return err;
}

static inline long
gcs_test_log_msg (gcs_test_log_t *log, const char *msg)
{
    long err = 0;
    err = fprintf (log->file, "%s\n", msg);
    return err;
}

gcs_conn_t *gcs = NULL;
gcs_to_t *to = NULL;

long msg_sent  = 0;
long msg_recvd = 0;
long msg_repld = 0;
long msg_len   = 0;

size_t size_sent  = 0;
size_t size_repld = 0;
size_t size_recvd = 0;

static inline long
test_recv_log_create(gcs_test_thread_t* thread)
{
    return
        snprintf (thread->log_msg, MAX_MSG_LEN,
                  "Thread %3ld(REPL): act_id = %llu, local_act_id = %llu, "
                  "len = %llu: %s",
                  thread->id,
                  (long long unsigned int)thread->act_id,
                  (long long unsigned int)thread->local_act_id,
                  (long long unsigned int)thread->msg_len, thread->msg);
}

static inline long
test_send_log_create(gcs_test_thread_t* thread)
{
    return
        snprintf (thread->log_msg, MAX_MSG_LEN,
                  "Thread %3ld (REPL): len = %llu, %s",
                  thread->id, (long long unsigned int) thread->msg_len,
                  thread->msg);
}

static inline long
test_log_msg (gcs_test_log_t* log, const char* msg)
{
    long ret;
    pthread_mutex_lock (&log->lock);
    ret = fprintf (recv_log->file, "%s\n", msg);
    pthread_mutex_lock (&log->lock);
    return ret;
}

static inline long
test_log_in_to (gcs_to_t* to, gcs_seqno_t seqno, const char* msg)
{
    long ret = 0;
    while ((ret = gcs_to_grab (to, seqno)) == -EAGAIN)
        usleep(10000);
    if (!ret) {// success
        if (msg != NULL) gcs_test_log_msg (recv_log, msg);
        ret = gcs_to_release (to, seqno);
    }
    return ret;
}

static inline long
test_send_last_applied (gcs_conn_t* gcs, gcs_seqno_t my_seqno)
{
    long ret = 0;
#define SEND_LAST_MASK ((1 << 10) - 1) // 1024
    if (!(my_seqno & SEND_LAST_MASK)) {
        gcs_seqno_t group_seqno;
            ret = gcs_set_last_applied (gcs, my_seqno);
            if (ret) {
                fprintf (stderr,"gcs_set_last_applied(%llu) returned %ld\n",
                         (unsigned long long)my_seqno, ret);
            }
            group_seqno = gcs_get_last_applied (gcs);
            if (!throughput) {
                fprintf (stdout, "Last applied: my = %llu, group = %llu\n",
                         (unsigned long long)my_seqno,
                         (unsigned long long)group_seqno);
            }
    }
    return ret;
}

static inline long
test_before_send (gcs_test_thread_t* thread)
{
    long ret = 0;

    /* create a message */
    thread->msg_len = test_make_msg (thread->msg, msg_len);
    if (thread->msg_len <= 0) return -1;
    
    if (!throughput) {
        /* log message before replication */
        ret = test_send_log_create (thread);
        ret = test_log_msg (send_log, thread->log_msg);
    }
    return ret;
}

static inline long
test_after_recv (gcs_test_thread_t* thread)
{
    long ret;

    if (!throughput) {
        /* log message after replication */
        ret = test_recv_log_create (thread);
        ret = test_log_in_to (to, thread->local_act_id, thread->log_msg);
    }
    else if (total) {
        ret = test_log_in_to (to, thread->local_act_id, NULL);
    }

    ret = test_send_last_applied (gcs, thread->local_act_id);

    return ret;
}

void *gcs_test_repl (void *arg)
{
    gcs_test_thread_t *thread = arg;
//    long i = thread->n_tries;
    long ret = 0;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
        ret = test_before_send (thread);
        if (ret < 0) break;

	/* replicate message */
	ret = gcs_repl (gcs, GCS_ACT_DATA, thread->msg_len,
                        (uint8_t*)thread->msg, &thread->act_id,
                        &thread->local_act_id);
	if (ret < 0) {
            assert (thread->act_id       == GCS_SEQNO_ILL);
            assert (thread->local_act_id == GCS_SEQNO_ILL);
            break;
        }

        msg_repld++;
        size_repld += thread->msg_len;
//	usleep ((rand() & 1) << 1);
        test_after_recv (thread);
//	puts (thread->log_msg); fflush (stdout);
    }
//    fprintf (stderr, "REPL thread %ld exiting: %s\n",
//             thread->id, gcs_strerror(ret));
    return NULL;
}

void *gcs_test_send (void *arg)
{
    long ret = 0;
    gcs_test_thread_t *thread = arg;
//    long i = thread->n_tries;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
        ret = test_before_send (thread);
        if (ret < 0) break;
	
	/* send message to group */
	ret = gcs_send (gcs, GCS_ACT_DATA, thread->msg_len,
                        (uint8_t*)thread->msg);
        if (ret < 0) break;
	//sleep (1);
        msg_sent++;
        size_sent += thread->msg_len;
    }
//    fprintf (stderr, "SEND thread %ld exiting: %s\n",
//             thread->id, gcs_strerror(ret));
    return NULL;
}

void *gcs_test_recv (void *arg)
{
    long ret = 0;
    gcs_test_thread_t *thread = arg;

    while (thread->n_tries)
    {
	/* receive message from group */
	ret = gcs_recv (gcs, &thread->act_type, &thread->msg_len,
                        (uint8_t **) &thread->msg, &thread->act_id,
                        &thread->local_act_id);
	
	if (ret < 0) {
            assert (thread->msg          == NULL);
            assert (thread->msg_len      == 0);
            assert (thread->act_id       == GCS_SEQNO_ILL);
            assert (thread->local_act_id == GCS_SEQNO_ILL);
            break;
	}
	msg_recvd++;
        size_recvd += thread->msg_len;

        test_after_recv (thread);
	//puts (thread->log_msg); fflush (stdout);
	free (thread->msg);
    }
//    fprintf (stderr, "RECV thread %ld exiting: %s\n",
//             thread->id, gcs_strerror(ret));
    return NULL;
}

static long gcs_test_thread_pool_start (gcs_test_thread_pool_t *pool)
{
    long i;
    long err = 0;
    void * (* thread_routine) (void *);

    switch (pool->type)
    {
    case GCS_TEST_REPL:
	thread_routine = gcs_test_repl;
	break;
    case GCS_TEST_SEND:
	thread_routine = gcs_test_send;
	break;
    case GCS_TEST_RECV:
	thread_routine = gcs_test_recv;
	break;
    default:
	error (0, 0, "Bad repl type %u\n", pool->type);
	return -1;
    }

    for (i = 0; i < pool->n_threads; i++)
    {
	if ((err = pthread_create (&pool->threads[i].thread, NULL,
				   thread_routine, &pool->threads[i])))
	    break;
    }
    pool->n_started = i;
    
    printf ("Started %ld threads of %s type (pool: %p)\n",
            pool->n_started,
	     GCS_TEST_REPL == pool->type ? "REPL" :
	    (GCS_TEST_SEND == pool->type ? "SEND" :"RECV"), pool);

    return 0;
}

static long gcs_test_thread_pool_join (const gcs_test_thread_pool_t *pool)
{
    long i;
    for (i = 0; i < pool->n_started; i++) {
	pthread_join (pool->threads[i].thread, NULL);
    }
    return 0;
}

static long gcs_test_thread_pool_stop (const gcs_test_thread_pool_t *pool)
{
    long i;
    for (i = 0; i < pool->n_started; i++) {
        pool->threads[i].n_tries = 0;
    }
    return 0;
}

long gcs_test_thread_pool_cancel (const gcs_test_thread_pool_t *pool)
{
    long i;
    printf ("Canceling pool: %p\n", pool); fflush(stdout);
    printf ("pool type: %u, pool threads: %ld\n", pool->type, pool->n_started);
    fflush(stdout);
    for (i = 0; i < pool->n_started; i++) {
        printf ("Cancelling %ld\n", i); fflush(stdout);
	pthread_cancel (pool->threads[i].thread);
        pool->threads[i].n_tries = 0;
    }
    return 0;
}

typedef struct gcs_test_conf
{
    long n_tries;
    long n_repl;
    long n_send;
    long n_recv;
    const char* backend;
}
gcs_test_conf_t;

static const char* DEFAULT_BACKEND = "dummy://";

static long gcs_test_conf (gcs_test_conf_t *conf, long argc, char *argv[])
{
    char *endptr;

    /* defaults */
    conf->n_tries = 10;
    conf->n_repl  = 10;
    conf->n_send  = 0;
    conf->n_recv  = 0;
    conf->backend = DEFAULT_BACKEND;

    switch (argc)
    {
    case 6:
	conf->n_recv = strtol (argv[5], &endptr, 10);
	if ('\0' != *endptr) goto error;
    case 5:
	conf->n_send = strtol (argv[4], &endptr, 10);
	if ('\0' != *endptr) goto error;
    case 4:
	conf->n_repl = strtol (argv[3], &endptr, 10);
	if ('\0' != *endptr) goto error;
    case 3:
	conf->n_tries = strtol (argv[2], &endptr, 10);
	if ('\0' != *endptr) goto error;
    case 2:
        conf->backend = argv[1];
	break;
    default:
	break;
    }
    
    printf ("Config: n_tries = %ld, n_repl = %ld, n_send = %ld, n_recv = %ld, "
            "backend = %s\n",
	    conf->n_tries, conf->n_repl, conf->n_send, conf->n_recv,
            conf->backend);

    return 0;
error:
    printf ("Usage: %s [backend] [tries:%ld] [repl threads:%ld] "
	    "[send threads: %ld] [recv threads: %ld]\n",
	    argv[0], conf->n_tries, conf->n_repl, conf->n_send, conf->n_recv);
    exit (EXIT_SUCCESS);
}

static inline void
test_print_stat (long msgs, size_t size, double interval)
{
    printf ("%7zu (%7.1f per sec.) / %7zuKb (%7.1f Kb/s)\n",
            msgs, (double)msgs/interval,
            size >> 10, (double)(size >> 10)/interval);
}

int main (int argc, char *argv[])
{
    long err = 0;
    gcs_test_conf_t conf;
    gcs_test_thread_pool_t repl_pool, send_pool, recv_pool;
    char *channel = "my_channel";
    struct timeval t_begin, t_end;

    gcs_conf_debug_on(); // turn on debug messages

    if ((err = gcs_test_conf     (&conf, argc, argv)))   goto out;
    if (!throughput) {
        if ((err = test_log_open (&send_log, SEND_LOG))) goto out;
        if ((err = test_log_open (&recv_log, RECV_LOG))) goto out;
    }

    to = gcs_to_create ((conf.n_repl + conf.n_recv + 1)*2, 1);
    if (!to) goto out;
//    total_tries = conf.n_tries * (conf.n_repl + conf.n_send);
    
    printf ("Opening connection: channel = %s, backend = %s\n",
             channel, conf.backend);

    if ((err = gcs_open (&gcs, channel, conf.backend))) goto out;
    printf ("Connected\n");

    msg_len = 1300;
    if (msg_len > MAX_MSG_LEN) msg_len = MAX_MSG_LEN; 
    gcs_conf_set_pkt_size (gcs, 7570); // to test fragmentation

    if ((err = gcs_test_thread_pool_create
	 (&repl_pool, GCS_TEST_REPL, conf.n_repl, conf.n_tries))) goto out;
    if ((err = gcs_test_thread_pool_create
	 (&send_pool, GCS_TEST_SEND, conf.n_send, conf.n_tries))) goto out;
    if ((err = gcs_test_thread_pool_create
	 (&recv_pool, GCS_TEST_RECV, conf.n_recv, conf.n_tries))) goto out;

    pthread_mutex_lock (&gcs_test_lock);

    gcs_test_thread_pool_start (&recv_pool);
    gcs_test_thread_pool_start (&repl_pool);
    gcs_test_thread_pool_start (&send_pool);

    printf ("Press any key to start the load:");
    fgetc (stdin);

    puts ("Started load\n");
    gettimeofday (&t_begin, NULL);
    printf ("Waiting for %ld seconds\n", conf.n_tries);
    fflush (stdout);
    pthread_mutex_unlock (&gcs_test_lock);

    usleep (conf.n_tries*1000000);

    puts ("Stopping threads");
    fflush(stdout); fflush(stderr);

    gcs_test_thread_pool_stop (&send_pool);
    gcs_test_thread_pool_stop (&repl_pool);
    puts ("Threads stopped");

    gcs_test_thread_pool_join (&send_pool);
    gcs_test_thread_pool_join (&repl_pool);
    puts ("Threads joined");

    gettimeofday (&t_end, NULL);
    {
        double interval = (t_end.tv_sec - t_begin.tv_sec) +
            0.000001*t_end.tv_usec - 0.000001*t_begin.tv_usec;

        printf ("Actions sent:       ");
        test_print_stat (msg_sent, size_sent, interval); 
        printf ("Actions received:   ");
        test_print_stat (msg_recvd, size_recvd, interval); 
        printf ("Actions replicated: ");
        test_print_stat (msg_repld, size_repld, interval);
        puts("---------------------------------------------------------------");
        printf ("Total throughput:    ");
        test_print_stat (msg_repld + msg_recvd, size_repld + size_recvd,
                         interval);
        printf ("Overhead at 10000 actions/sec: %5.2f%%\n",
                1000000.0 * interval / (msg_repld + msg_recvd));
        puts("");
    }

    printf ("Press any key to exit the program:\n");
    fgetc (stdin);

    /* Cancelling recv thread here in case we receive messages from other node*/
    gcs_test_thread_pool_cancel (&recv_pool);
    gcs_test_thread_pool_join (&recv_pool);

    printf ("Closing GCS connection...");
//    gcs_test_thread_pool_stop (&recv_pool);
    if ((err = gcs_close (&gcs))) goto out;
    printf ("done\n"); fflush (stdout);
    printf ("Disconnected\n");

    gcs_test_thread_pool_destroy (&repl_pool);
    gcs_test_thread_pool_destroy (&send_pool);
    gcs_test_thread_pool_destroy (&recv_pool);

    gcs_to_destroy(&to);

    if (!throughput) {
        printf ("Closing send log\n");
        test_log_close (&send_log);
        printf ("Closing recv log\n");
        test_log_close (&recv_log);
    }

    {
        ssize_t total;
        ssize_t allocs;
        ssize_t deallocs;

        void gu_mem_stats (ssize_t*, ssize_t*, ssize_t*);
        gu_mem_stats (&total, &allocs, &deallocs);
        printf ("Memory statistics:\n"
                "Memory still allocated: %10lld\n"
                "Times allocated:        %10lld\n"
                "Times freed:            %10lld\n",
                (long long)total,
		(long long)allocs,
		(long long)deallocs);
    }

    return 0;
out:
    printf ("Error: %ld(%s)\n", err, gcs_strerror (err));
    return err;
}
