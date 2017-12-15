/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
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
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include <galerautils.h>

#include "gcs.hpp"
#include "gcs_test.hpp"

#define USE_WAIT

#define gcs_malloc(a) ((a*) malloc (sizeof (a)))

static pthread_mutex_t gcs_test_lock = PTHREAD_MUTEX_INITIALIZER;

static gcache_t* gcache = NULL;

typedef struct gcs_test_log
{
    FILE *file;
    pthread_mutex_t lock;
}
gcs_test_log_t;

#define SEND_LOG "/dev/shm/gcs_test_send.log"
#define RECV_LOG "/dev/shm/gcs_test_recv.log"

static gcs_test_log_t *send_log, *recv_log;

static bool throughput = true; // bench for throughput
static bool total      = true; // also enable TO locking

typedef enum
{
    GCS_TEST_SEND,
    GCS_TEST_RECV,
    GCS_TEST_REPL
}
gcs_test_repl_t;

typedef struct gcs_test_thread
{
    pthread_t         thread;
    long              id;
    struct gcs_action act;
    long              n_tries;
    void*             msg;
    char*             log_msg;
}
gcs_test_thread_t;

#define MAX_MSG_LEN (1 << 16)

static long gcs_test_thread_create (gcs_test_thread_t *t, long id, long n_tries)
{
    t->id           = id;
    t->msg          = calloc (MAX_MSG_LEN, sizeof(char));
    t->act.buf      = t->msg;
    t->act.size     = MAX_MSG_LEN;
    t->act.seqno_g  = GCS_SEQNO_ILL;
    t->act.seqno_l  = GCS_SEQNO_ILL;
    t->act.type     = GCS_ACT_TORDERED;
    t->n_tries      = n_tries;

    if (t->msg)
    {
        t->log_msg = (char*)calloc (MAX_MSG_LEN, sizeof(char));
        if (t->log_msg) return 0;
    }

    return -ENOMEM;
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
	err = errno;
	fprintf (stderr, "Failed to allocate %ld thread objects: %ld (%s)\n",
	         n_threads, err, strerror(err));
	goto out1;
    }

    for (i = 0; i < pool->n_threads; i++)
    {
	if ((err = gcs_test_thread_create (pool->threads + i, i, n_tries)))
	{
	    err = errno;
	    fprintf (stderr, "Failed to create thread object %ld: %ld (%s)\n",
	             i, err, strerror(err));
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
    long len = 0;

    if (!throughput) {
        pthread_mutex_lock   (&make_msg_lock);
        count++;
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

    snprintf (real_name, 1024, "%s.%lld", name, (long long)getpid());
    // cppcheck-suppress memleak
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
gu_to_t    *to  = NULL;

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
    return snprintf (thread->log_msg, MAX_MSG_LEN - 1,
                     "Thread %3ld(REPL): act_id = %lld, local_act_id = %lld, "
                     "len = %lld: %s",
                     thread->id,
                     (long long)thread->act.seqno_g,
                     (long long)thread->act.seqno_l,
                     (long long)thread->act.size,
                     (const char*)thread->act.buf);
}

static inline long
test_send_log_create(gcs_test_thread_t* thread)
{
    return snprintf (thread->log_msg, MAX_MSG_LEN - 1,
                     "Thread %3ld (REPL): len = %lld, %s",
                     thread->id,
                     (long long) thread->act.size,
                     (const char*)thread->act.buf);
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
test_log_in_to (gu_to_t* to, gcs_seqno_t seqno, const char* msg)
{
    long ret = 0;
    while ((ret = gu_to_grab (to, seqno)) == -EAGAIN)
        usleep(10000);
    if (!ret) {// success
        if (msg != NULL) gcs_test_log_msg (recv_log, msg);
        ret = gu_to_release (to, seqno);
    }
    return ret;
}

static gcs_seqno_t group_seqno = 0;

static inline long
test_send_last_applied (gcs_conn_t* gcs, gcs_seqno_t my_seqno)
{
    long ret = 0;

#define SEND_LAST_MASK ((1 << 14) - 1) // every 16K seqno

    if (!(my_seqno & SEND_LAST_MASK)) {
            ret = gcs_set_last_applied (gcs, my_seqno);
            if (ret) {
                fprintf (stderr,"gcs_set_last_applied(%lld) returned %ld\n",
                         (long long)my_seqno, ret);
            }
//            if (!throughput) {
                fprintf (stdout, "Last applied: my = %lld, group = %lld\n",
                         (long long)my_seqno, (long long)group_seqno);
//            }
    }
    return ret;
}

static inline long
test_before_send (gcs_test_thread_t* thread)
{
#ifdef USE_WAIT
    static const struct timespec wait = { 0, 10000000 };
#endif
    long ret = 0;

    /* create a message */
    thread->act.size = test_make_msg ((char*)thread->msg, msg_len);
    thread->act.buf  = thread->msg;
    if (thread->act.size <= 0) return -1;

    if (!throughput) {
        /* log message before replication */
        ret = test_send_log_create (thread);
        ret = test_log_msg (send_log, thread->log_msg);
    }
#ifdef USE_WAIT
    while ((ret = gcs_wait(gcs)) && ret > 0) nanosleep (&wait, NULL);
#endif
    return ret;
}

static inline long
test_after_recv (gcs_test_thread_t* thread)
{
    long ret;

    if (!throughput) {
        /* log message after replication */
        ret = test_recv_log_create (thread);
        ret = test_log_in_to (to, thread->act.seqno_l, thread->log_msg);
    }
    else if (total) {
        ret = test_log_in_to (to, thread->act.seqno_l, NULL);
    }
    else {
        gu_to_self_cancel (to, thread->act.seqno_l);
    }

    ret = test_send_last_applied (gcs, thread->act.seqno_g);
//    fprintf (stdout, "SEQNO applied %lld", thread->local_act_id);

    if (thread->act.type == GCS_ACT_TORDERED)
        gcache_free (gcache, thread->act.buf);

    return ret;
}

void *gcs_test_repl (void *arg)
{
    gcs_test_thread_t *thread = (gcs_test_thread_t*)arg;
//    long i = thread->n_tries;
    long ret = 0;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
        ret = test_before_send (thread);
        if (ret < 0) break;

        /* replicate message */
        ret = gcs_repl (gcs, &thread->act, false);

        if (ret < 0) {
            assert (thread->act.seqno_g == GCS_SEQNO_ILL);
            assert (thread->act.seqno_l == GCS_SEQNO_ILL);
            break;
        }

        msg_repld++;
        size_repld += thread->act.size;
//      usleep ((rand() & 1) << 1);
        test_after_recv (thread);
//      puts (thread->log_msg); fflush (stdout);
    }
//    fprintf (stderr, "REPL thread %ld exiting: %s\n",
//             thread->id, strerror(-ret));
    return NULL;
}

void *gcs_test_send (void *arg)
{
    long ret = 0;
    gcs_test_thread_t *thread = (gcs_test_thread_t*)arg;
//    long i = thread->n_tries;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
        ret = test_before_send (thread);
        if (ret < 0) break;

    /* send message to group */
        ret = gcs_send (gcs, thread->act.buf, thread->act.size,
                        GCS_ACT_TORDERED, false);

        if (ret < 0) break;
        //sleep (1);
        msg_sent++;
        size_sent += thread->act.size;
    }
//    fprintf (stderr, "SEND thread %ld exiting: %s\n",
//             thread->id, strerror(-ret));
    return NULL;
}

static void
gcs_test_handle_configuration (gcs_conn_t* gcs, gcs_test_thread_t* thread)
{
    long ret;
    static gcs_seqno_t conf_id = 0;
    gcs_act_conf_t* conf = (gcs_act_conf_t*)thread->msg;
    gu_uuid_t ist_uuid = {{0, }};
    gcs_seqno_t ist_seqno = GCS_SEQNO_ILL;

    fprintf (stdout, "Got GCS_ACT_CONF: Conf: %lld, "
             "seqno: %lld, members: %ld, my idx: %ld, local seqno: %lld\n",
             (long long)conf->conf_id, (long long)conf->seqno,
             conf->memb_num, conf->my_idx, (long long)thread->act.seqno_l);
    fflush (stdout);

    // NOTE: what really needs to be checked is seqno and group_uuid, but here
    //       we don't keep track of them (and don't do real transfers),
    //       so for simplicity, just check conf_id.
    while (-EAGAIN == (ret = gu_to_grab (to, thread->act.seqno_l)));
    if (0 == ret) {
        if (conf->my_state == GCS_NODE_STATE_PRIM) {
            gcs_seqno_t seqno, s;
            fprintf (stdout,"Gap in configurations: ours: %lld, group: %lld.\n",
                     (long long)conf_id, (long long)conf->conf_id);
            fflush (stdout);

            fprintf (stdout, "Requesting state transfer up to %lld: %s\n",
                     (long long)conf->seqno, // this is global seqno
                     strerror (-gcs_request_state_transfer (gcs, 0, &conf->seqno,
                                                            sizeof(conf->seqno),
                                                            "", &ist_uuid, ist_seqno,
                                                            &seqno)));

            // pretend that state transfer is complete, cancel every action up
            // to seqno
            for (s = thread->act.seqno_l + 1; s <= seqno; s++) {
                gu_to_self_cancel (to, s); // this is local seqno
            }

            fprintf (stdout, "Sending JOIN: %s\n", strerror(-gcs_join(gcs, 0)));
            fflush (stdout);
        }

        gcs_resume_recv (gcs);
        gu_to_release (to, thread->act.seqno_l);
    }
    else {
        fprintf (stderr, "Failed to grab TO: %ld (%s)", ret, strerror(ret));
    }
    conf_id = conf->conf_id;
}

void *gcs_test_recv (void *arg)
{
    long ret = 0;
    gcs_test_thread_t *thread = (gcs_test_thread_t*)arg;

    while (thread->n_tries)
    {
        /* receive message from group */
        while ((ret = gcs_recv (gcs, &thread->act)) == -ECANCELED) {
            usleep (10000);
        }

        if (ret <= 0) {
            fprintf (stderr, "gcs_recv() %s: %ld (%s). Thread exits.\n",
                     ret < 0 ? "failed" : "connection closed",
                     ret, strerror(-ret));
            assert (thread->act.buf     == NULL);
            assert (thread->act.size    == 0);
            assert (thread->act.seqno_g == GCS_SEQNO_ILL);
            assert (thread->act.seqno_l == GCS_SEQNO_ILL);
            assert (thread->act.type    == GCS_ACT_ERROR);
            break;
        }

        assert (thread->act.type < GCS_ACT_ERROR);

        msg_recvd++;
        size_recvd += thread->act.size;

        switch (thread->act.type) {
        case GCS_ACT_TORDERED:
            test_after_recv (thread);
            //puts (thread->log_msg); fflush (stdout);
            break;
        case GCS_ACT_COMMIT_CUT:
            group_seqno = *(gcs_seqno_t*)thread->act.buf;
            gu_to_self_cancel (to, thread->act.seqno_l);
            break;
        case GCS_ACT_CONF:
            gcs_test_handle_configuration (gcs, thread);
            break;
        case GCS_ACT_STATE_REQ:
            fprintf (stdout, "Got STATE_REQ\n");
            gu_to_grab (to, thread->act.seqno_l);
            fprintf (stdout, "Sending JOIN: %s\n", strerror(-gcs_join(gcs, 0)));
            fflush (stdout);
            gu_to_release (to, thread->act.seqno_l);
            break;
        case GCS_ACT_JOIN:
            fprintf (stdout, "Joined\n");
            gu_to_self_cancel (to, thread->act.seqno_l);
            break;
        case GCS_ACT_SYNC:
            fprintf (stdout, "Synced\n");
            gu_to_self_cancel (to, thread->act.seqno_l);
            break;
        default:
            fprintf (stderr, "Unexpected action type: %d\n", thread->act.type);

        }
    }
//    fprintf (stderr, "RECV thread %ld exiting: %s\n",
//             thread->id, strerror(-ret));
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
        fprintf (stderr, "Bad repl type %u\n", pool->type);
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
            (GCS_TEST_SEND == pool->type ? "SEND" :"RECV"), (void*)pool);

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
    printf ("Canceling pool: %p\n", (void*)pool); fflush(stdout);
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
    conf->n_recv  = 1;
    conf->backend = DEFAULT_BACKEND;

    switch (argc)
    {
    case 6:
        conf->n_recv = strtol (argv[5], &endptr, 10);
        if ('\0' != *endptr) goto error;
        // fall through
    case 5:
        conf->n_send = strtol (argv[4], &endptr, 10);
        if ('\0' != *endptr) goto error;
        // fall through
    case 4:
        conf->n_repl = strtol (argv[3], &endptr, 10);
        if ('\0' != *endptr) goto error;
        // fall through
    case 3:
        conf->n_tries = strtol (argv[2], &endptr, 10);
        if ('\0' != *endptr) goto error;
        // fall through
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
    printf ("%7ld (%7.1f per sec.) / %7zuKb (%7.1f Kb/s)\n",
            msgs, (double)msgs/interval,
            size >> 10, (double)(size >> 10)/interval);
}

int main (int argc, char *argv[])
{
    long err = 0;
    gcs_test_conf_t conf;
    gcs_test_thread_pool_t repl_pool, send_pool, recv_pool;
    const char *channel = "my_channel";
    struct timeval t_begin, t_end;
    gu_config_t* gconf;
    bool bstrap;

    gcs_conf_debug_on(); // turn on debug messages

    if ((err = gcs_test_conf     (&conf, argc, argv)))   goto out;
    if (!throughput) {
        if ((err = test_log_open (&send_log, SEND_LOG))) goto out;
        if ((err = test_log_open (&recv_log, RECV_LOG))) goto out;
    }

    to = gu_to_create ((conf.n_repl + conf.n_recv + 1)*2, GCS_SEQNO_FIRST);
    if (!to) goto out;
//    total_tries = conf.n_tries * (conf.n_repl + conf.n_send);

    printf ("Opening connection: channel = %s, backend = %s\n",
             channel, conf.backend);

    gconf = gu_config_create ();
    if (!gconf) goto out;

    if (gu_config_add(gconf, "gcache.size", "0")) goto out;
    if (gu_config_add(gconf, "gcache.page_size", "1M")) goto out;

    if (!(gcache = gcache_create (gconf, ""))) goto out;
    if (!(gcs = gcs_create (gconf, gcache, NULL, NULL, 0, 0))) goto out;
    puts ("debug"); fflush(stdout);
    /* the following hack won't work if there is 0.0.0.0 in URL options */
    bstrap = (NULL != strstr(conf.backend, "0.0.0.0"));
    if ((err  = gcs_open   (gcs, channel, conf.backend, bstrap))) goto out;
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

    puts ("Started load.");
    gettimeofday (&t_begin, NULL);
    printf ("Waiting for %ld seconds\n", conf.n_tries);
    fflush (stdout);
    pthread_mutex_unlock (&gcs_test_lock);

    usleep (conf.n_tries*1000000);

    puts ("Stopping SEND and REPL threads...");
    fflush(stdout); fflush(stderr);

    gcs_test_thread_pool_stop (&send_pool);
    gcs_test_thread_pool_stop (&repl_pool);
    puts ("Threads stopped.");

    gcs_test_thread_pool_join (&send_pool);
    gcs_test_thread_pool_join (&repl_pool);
    puts ("SEND and REPL threads joined.");

    printf ("Closing GCS connection... ");
    if ((err = gcs_close (gcs))) goto out;
    puts ("done.");

    gcs_test_thread_pool_join (&recv_pool);
    puts ("RECV threads joined.");

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

    printf ("Freeing GCS connection handle...");
    if ((err = gcs_destroy (gcs))) goto out;
    gcs = NULL;
    printf ("done\n"); fflush (stdout);

    printf ("Destroying GCache object:\n");
    gcache_destroy (gcache);

    gcs_test_thread_pool_destroy (&repl_pool);
    gcs_test_thread_pool_destroy (&send_pool);
    gcs_test_thread_pool_destroy (&recv_pool);

    gu_to_destroy(&to);

    if (!throughput) {
        printf ("Closing send log\n");
        test_log_close (&send_log);
        printf ("Closing recv log\n");
        test_log_close (&recv_log);
    }

    {
        ssize_t total;
        ssize_t allocs;
        ssize_t reallocs;
        ssize_t deallocs;

        void gu_mem_stats (ssize_t*, ssize_t*, ssize_t*, ssize_t*);
        gu_mem_stats (&total, &allocs, &reallocs, &deallocs);
        printf ("Memory statistics:\n"
                "Memory still allocated: %10lld\n"
                "Times allocated:        %10lld\n"
                "Times reallocated:      %10lld\n"
                "Times freed:            %10lld\n",
                (long long)total,
		(long long)allocs,
		(long long)reallocs,
		(long long)deallocs);
    }

    return 0;
out:
    printf ("Error: %ld (%s)\n", err, strerror (-err));
    return err;
}
