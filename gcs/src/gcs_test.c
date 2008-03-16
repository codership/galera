// Copyright (C) 2007 Codership Oy <info@codership.com>
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
    int             id;
    gcs_test_repl_t type;
    gcs_seqno_t     act_id;
    gcs_seqno_t     local_act_id;
    gcs_act_type_t  act_type;
    int             n_tries;
    size_t          msg_len;
    char           *msg;
    char           *log_msg;
}
gcs_test_thread_t;

#define MAX_MSG_LEN 8192
static int gcs_test_thread_create (gcs_test_thread_t *t, int id, int n_tries)
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

static int gcs_test_thread_destroy (gcs_test_thread_t *t)
{
    if (t->msg)     free (t->msg);
    if (t->log_msg) free (t->log_msg);
    return 0;
}

typedef struct gcs_test_thread_pool
{
    int               n_threads;
    int               n_tries;
    int               n_started;
    gcs_test_repl_t   type;
    gcs_test_thread_t *threads;
}
gcs_test_thread_pool_t;

static int gcs_test_thread_pool_create (gcs_test_thread_pool_t *pool,
					const gcs_test_repl_t   type,
					const int               n_threads,
					const int               n_tries)
{
    int err = 0;
    int i;

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
	       "Failed to allocate %d thread objects", n_threads);
	err = errno; 
	goto out1;
    }

    for (i = 0; i < pool->n_threads; i++)
    {
	if ((err = gcs_test_thread_create (pool->threads + i, i, n_tries)))
	{
	    error (0, errno,
		   "Failed to create thread object %d", i);
	    err = errno;
	    goto out2;
	}
    }
    printf ("Created %d thread objects\n", i);
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

static pthread_mutex_t make_msg_lock = PTHREAD_MUTEX_INITIALIZER;
//static int total_tries;

static int gcs_test_make_msg (char* msg, const long mlen)
{
    static gcs_seqno_t count = 1;
    size_t len = 0;
    gcs_seqno_t tmp;

    pthread_mutex_lock   (&make_msg_lock);
    tmp = count++;
    pthread_mutex_unlock (&make_msg_lock);

    len = snprintf (msg, mlen, "%10d %9lld %s",
                    rand(), (long long)count++, gcs_test_data);

    if (len >= mlen)
        return mlen;
    else
        return len;
}

static int gcs_test_log_open (gcs_test_log_t **log, const char *name)
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

static int gcs_test_log_close (gcs_test_log_t **log)
{
    int err = 0;
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

static int gcs_test_log_msg (gcs_test_log_t *log, const char *msg)
{
    int err = 0;
    pthread_mutex_lock (&log->lock);
    err = fprintf (log->file, "%s\n", msg);
//    fflush (log->file);
    pthread_mutex_unlock (&log->lock);
    return err;
}

gcs_conn_t *gcs = NULL;
gcs_to_t *to = NULL;

long msg_sent  = 0;
long msg_recvd = 0;
long msg_repld = 0;
long msg_len   = 0;

void *gcs_test_repl (void *arg)
{
    gcs_test_thread_t *thread = arg;
//    int i = thread->n_tries;
    int ret = 0;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
	/* create a message */
	thread->msg_len = gcs_test_make_msg (thread->msg, msg_len);
	if (thread->msg_len <= 0) break;
	
	/* log message before replication */
	snprintf (thread->log_msg, MAX_MSG_LEN,
		  "Thread %3d (repl): %s", thread->id, thread->msg);
	gcs_test_log_msg (send_log, thread->log_msg);
//	puts (thread->log_msg); fflush (stdout);

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

//	usleep ((rand() & 1) << 1);
	
	/* log message after replication */	  
	snprintf (thread->log_msg, MAX_MSG_LEN,
		  "Thread %3d (repl): %s, act_id = %llu, local_act_id = %llu",
		  thread->id,thread->msg,
		  (long long unsigned int)thread->act_id,
		  (long long unsigned int)thread->local_act_id);
	gcs_to_grab      (to, thread->local_act_id);
	gcs_test_log_msg (recv_log, thread->log_msg);
	gcs_to_release   (to, thread->local_act_id);
//	puts (thread->log_msg); fflush (stdout);
    }
    fprintf (stderr, "REPL thread %d exiting: %s\n",
             thread->id, gcs_strerror(ret));
    return NULL;
}

void *gcs_test_send (void *arg)
{
    int ret = 0;
    gcs_test_thread_t *thread = arg;
//    int i = thread->n_tries;

    pthread_mutex_lock   (&gcs_test_lock);
    pthread_mutex_unlock (&gcs_test_lock);

    while (thread->n_tries)
    {
	/* create message */
	thread->msg_len = gcs_test_make_msg (thread->msg, msg_len);
	
	/* log message before replication */
	snprintf (thread->log_msg, MAX_MSG_LEN,
		  "Thread %3d (send): %s", thread->id, thread->msg);
	gcs_test_log_msg (send_log, thread->log_msg);
//	puts (thread->log_msg);
	
	/* send message to group */
	ret = gcs_send (gcs, GCS_ACT_DATA, thread->msg_len,
                        (uint8_t*)thread->msg);
        if (ret < 0) break;
	//sleep (1);
        msg_sent++;
    }
    fprintf (stderr, "SEND thread %d exiting: %s\n",
             thread->id, gcs_strerror(ret));
    return NULL;
}

void *gcs_test_recv (void *arg)
{
    int ret = 0;
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

	/* log message after replication */
	snprintf (thread->log_msg, MAX_MSG_LEN,
		  "Thread %3d (recv): %s, act_id = %llu, local_act_id = %llu",
		  thread->id, thread->msg,
		  (long long unsigned int)thread->act_id,
		  (long long unsigned int)thread->local_act_id);
	gcs_to_grab      (to, thread->local_act_id);
	gcs_test_log_msg (recv_log, thread->log_msg);
	gcs_to_release   (to, thread->local_act_id);
//	puts (thread->log_msg); fflush (stdout);
	free (thread->msg);
    }
    fprintf (stderr, "RECV thread %d exiting: %s\n",
             thread->id, gcs_strerror(ret));
    return NULL;
}

static int gcs_test_thread_pool_start (gcs_test_thread_pool_t *pool)
{
    int i;
    int err = 0;
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
	error (0, 0, "Bad repl type %d\n", pool->type);
	return -1;
    }

    for (i = 0; i < pool->n_threads; i++)
    {
	if ((err = pthread_create (&pool->threads[i].thread, NULL,
				   thread_routine, &pool->threads[i])))
	    break;
    }
    pool->n_started = i;
    
    printf ("Started %d threads of %s type\n", pool->n_started,
	     GCS_TEST_REPL == pool->type ? "REPL" :
	    (GCS_TEST_SEND == pool->type ? "SEND" :"RECV"));

    return 0;
}

static int gcs_test_thread_pool_join (const gcs_test_thread_pool_t *pool)
{
    int i;
    for (i = 0; i < pool->n_started; i++)
	pthread_join (pool->threads[i].thread, NULL);
    return 0;
}

static int gcs_test_thread_pool_stop (const gcs_test_thread_pool_t *pool)
{
    int i;
    for (i = 0; i < pool->n_started; i++)
        pool->threads[i].n_tries = 0;
    return 0;
}

static int gcs_test_thread_pool_cancel (const gcs_test_thread_pool_t *pool)
{
    int i;
    for (i = 0; i < pool->n_started; i++)
	pthread_cancel ((pool->threads[i].thread));
        pool->threads[i].n_tries = 0;
    return 0;
}

typedef struct gcs_test_conf
{
    int n_tries;
    int n_repl;
    int n_send;
    int n_recv;
    gcs_backend_type_t backend;
}
gcs_test_conf_t;

static int gcs_test_conf (gcs_test_conf_t *conf, int argc, char *argv[])
{
    char *endptr;

    /* defaults */
    conf->n_tries = 10;
    conf->n_repl  = 10;
    conf->n_send  = 0;
    conf->n_recv  = 0;
    conf->backend = GCS_BACKEND_DUMMY;

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
	if (!strcmp (argv[1], "dummy"))
	    conf->backend = GCS_BACKEND_DUMMY;
	else if (!strcmp (argv[1], "spread"))
	    conf->backend = GCS_BACKEND_SPREAD;
	else if (!strcmp(argv[1], "vs"))
	    conf->backend = GCS_BACKEND_VS;
	else
	    goto error;
	break;
    default:
	break;
    }
    
    printf ("Config: n_tries = %d, n_repl = %d, n_send = %d, n_recv = %d\n",
	    conf->n_tries, conf->n_repl, conf->n_send, conf->n_recv);

    return 0;
error:
    printf ("Usage: %s [dummy|spread|vs] [tries:%d] [repl threads:%d] "
	    "[send threads: %d] [recv threads: %d]\n",
	    argv[0], conf->n_tries, conf->n_repl, conf->n_send, conf->n_recv);
    exit (EXIT_SUCCESS);
}

int main (int argc, char *argv[])
{
    int err = 0;
    gcs_test_conf_t conf;
    gcs_test_thread_pool_t repl_pool, send_pool, recv_pool;
    char *channel = "my_channel";
    char *socket  = "192.168.0.1:4803";
    struct timeval t_begin, t_end;

    if (getenv("GCS_TEST_SOCKET"))
	socket = getenv("GCS_TEST_SOCKET");

    gcs_conf_debug_on(); // turn on debug messages

    if ((err = gcs_test_conf     (&conf, argc, argv)))   goto out;
    if ((err = gcs_test_log_open (&send_log, SEND_LOG))) goto out;
    if ((err = gcs_test_log_open (&recv_log, RECV_LOG))) goto out;

    to = gcs_to_create (conf.n_repl + conf.n_recv + 1, 1);
    if (!to) goto out;
//    total_tries = conf.n_tries * (conf.n_repl + conf.n_send);
    
    printf ("Opening connection: channel = %s, socket = %s, backend = %d\n",
             channel, socket, conf.backend);

    if ((err = gcs_open (&gcs, channel, socket, conf.backend))) goto out;
    printf ("Connected\n");
    msg_len = 1300; if (msg_len > MAX_MSG_LEN) msg_len = MAX_MSG_LEN; 
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

    gettimeofday (&t_begin, NULL);
    pthread_mutex_unlock (&gcs_test_lock);
    puts ("Started load\n");
    printf ("Waiting for %d seconds\n", conf.n_tries);
    fflush (stdout);

    sleep (conf.n_tries);

    gcs_test_thread_pool_stop (&send_pool);
    gcs_test_thread_pool_stop (&repl_pool);
    gcs_test_thread_pool_join (&send_pool);
    gcs_test_thread_pool_join (&repl_pool);
    gettimeofday (&t_end, NULL);
    {
        double interval = (t_end.tv_sec - t_begin.tv_sec) +
            0.000001*t_end.tv_usec - 0.000001*t_begin.tv_usec;

        printf ("Messages sent:       %ld (%6.2f per sec.)\n",
                msg_sent, (double)msg_sent/interval);
        printf ("Messages received:   %ld (%6.2f per sec.)\n",
                msg_recvd, (double)msg_recvd/interval);
        printf ("Messages replicated: %ld (%6.2f per sec.)\n\n",
                msg_repld, (double)msg_repld/interval);
    }
    printf ("Press any key to exit the program:\n");
    fgetc (stdin);

    gcs_test_thread_pool_cancel (&recv_pool);
    printf ("Closing GCS connection...");
//    gcs_test_thread_pool_stop (&recv_pool);
    if ((err = gcs_close (&gcs))) goto out;
    printf ("done\n"); fflush (stdout);
    gcs_test_thread_pool_join (&recv_pool);
    printf ("Disconnected\n");

    printf ("Closing send log\n");
    gcs_test_log_close (&send_log);
    printf ("Closing recv log\n");
    gcs_test_log_close (&recv_log);

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
                (long long int)total,
		(long long int)allocs,
		(long long int)deallocs);
    }

    return 0;
out:
    printf ("Error: %d(%s)\n", err, gcs_strerror (err));
    return err;
}
