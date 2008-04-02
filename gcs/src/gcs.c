// Copyright (C) 2007 Codership Oy <info@codership.com>

/*
 * Top-level application interface implementation.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <galerautils.h>

#include "gcs.h"
#include "gcs_generic.h"
#include "gcs_fifo.h"
#include "gcs_queue.h"

#define GCS_MAX_REPL_THREADS 16384

typedef enum
{
    GCS_CONN_OPEN,
    GCS_CONN_CLOSED
}
gcs_conn_state_t;

struct gcs_conn
{
    int    my_id;
    char  *my_name;
    char  *channel;
    char  *socket;

    volatile gcs_conn_state_t state;
    volatile int err;
    gu_mutex_t   lock;

    volatile gcs_seqno_t local_act_id; /* local seqno of the action  */

    /* A queue for threads waiting for replicated actions */
    gcs_fifo_t*  repl_q;
    gu_thread_t  send_thread;
    gu_mutex_t   repl_lock; /*! this lock is needed to share the lock_q
                             *  with gcs_recv_thread() */

    /* A queue for threads waiting for received actions */
    gcs_queue_t* recv_q;
    gu_thread_t  recv_thread;

//    /* A condition for threads which called gcs_recv()  */
//    gu_cond_t *recv_cond;

    gcs_generic_conn_t *generic; // the context that is returned by
                   // the generic group communication system
};

typedef struct gcs_act
{
    size_t         act_size;
    gcs_act_type_t act_type;
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    uint8_t*       action;
    gu_mutex_t     wait_mutex;
    gu_cond_t      wait_cond;
}
gcs_act_t;

/*
 * gcs_recv_thread() receives whatever actions arrive from group,
 * and performs necessary actions based on action type.
 */
static void *gcs_recv_thread (void *arg)
{
    long           ret  = -ENOTCONN;
    gcs_conn_t    *conn = arg;
    gcs_act_t     *act  = NULL;
    gcs_seqno_t    act_id;
    gcs_act_type_t act_type;
    size_t         act_size;
    uint8_t       *action;
    uint8_t       *action_repl = NULL;

//    gu_debug ("Starting RECV thread");

    while (conn->state == GCS_CONN_OPEN)
    {
	if ((act_size = gcs_generic_recv (conn->generic,
					  &action,
					  &act_type,
					  &act_id)) < 0) {
	    ret = act_size;
	    gu_debug ("gcs_generic_recv returned %d: %s", gcs_strerror(ret));
	    break;
	}

        if (act_type <= GCS_ACT_DATA) { // currently app doesn't handle
                                        // anything else. Fixme.
	    /* deliver to application and increment local order */
	    conn->local_act_id++;
//            gu_debug ("Received action #%llu", conn->local_act_id);
        } else {
	    /* not interested, continue loop */
	    if (action) free (action); // was allocated by standard malloc()
//            gu_debug ("Ignoring action after #%llu", conn->local_act_id);
	    continue;
        }

	if (!action_repl) {
	    /* Check if there is any local repl action in queue */
	    act = gcs_fifo_head (conn->repl_q);
	    if (act) {
                /* action_repl will be used to determine, whether
                 * we deliver action to REPL application thread
                 * or to RECV application thread */
		action_repl = act->action;
	    }	    
	}

	/* Since this is the only way, NULL-sized actions cannot be REPL'ed */
	if (action_repl           /* there is REPL thread waiting for action */
            &&
            action == action_repl) /* this is the action it is waiting for */
	{
	    /* local action */
//            gu_debug ("Local action");
	    act = gcs_fifo_safe_get (conn->repl_q);
	    
	    act->act_id       = act_id;
//	    act->act_id       = conn->local_act_id;
	    act->local_act_id = conn->local_act_id;
	    assert (act->action == action);

	    gu_mutex_lock   (&act->wait_mutex);
	    gu_cond_signal  (&act->wait_cond);
	    gu_mutex_unlock (&act->wait_mutex);

	    action_repl = NULL;
	    act  = NULL;
	}
	else
	{
	    /* slave action */
//            gu_debug ("Foreign action");
/** @todo   Implement mallocless queue for RECV threads and
//          (mostly) mallocless buffering for actions.
//          Otherwise we're having two extra mallocs here.
//          Perhaps mallocless queue for RECV threads will do.
//          Action buffer must be infinite, so it has to be a list.
//          However we can make it action specific and avoid 1 malloc.
*/
	    /* allocate new action structure */
	    act = GU_MALLOC (gcs_act_t);
	    if (NULL == act) {
		ret = -ENOMEM; break;
	    }

	    act->act_size     = act_size;
	    act->act_type     = act_type;
	    act->act_id       = act_id;
//	    act->act_id       = conn->local_act_id;
            act->local_act_id = conn->local_act_id;
	    act->action       = action;

//            gu_mutex_lock (&conn->recv_lock);
//            act = gcs_fifo_put (conn->recv_q, act);
	    if ((ret = gcs_queue_push (conn->recv_q, act))) {
		break;
            }
	    act = NULL;
	}
        // below I can't use any references to act
//       gu_debug("Received action of type %d, size %d, id=%llu, local id=%llu",
//                 act_type, act_size, act_id, conn->local_act_id);
    }
    
    gu_debug ("RECV thread exiting %d: %s", ret, gcs_strerror(ret));
    return NULL;
}

/* Creates a group/channel connection context*/
int gcs_open (gcs_conn_t **gcs, const char *channel, const char *backend)
{
    long err = 0;
    gcs_conn_t *conn = GU_CALLOC (1, gcs_conn_t);

    if (NULL == conn) return errno;
    conn->state = -GCS_CONN_CLOSED;

    /* Some conn fields must be initialized during this call */
    if ((err = gcs_generic_open (&conn->generic, channel, backend)))
	return err;

    if ((err = gcs_generic_set_pkt_size (conn->generic, GCS_DEFAULT_PKT_SIZE)))
	return err;

    conn->repl_q  = gcs_fifo_create (GCS_MAX_REPL_THREADS);
    conn->recv_q  = gcs_queue ();
    conn->state   = GCS_CONN_OPEN;

    if ((err = gu_thread_create (&conn->recv_thread,
				 NULL,
				 gcs_recv_thread,
				 conn)))
	return err;
    
    gu_mutex_init (&conn->lock,      NULL);
    gu_mutex_init (&conn->repl_lock, NULL);
    *gcs = conn;
    return 0;
}

/* Frees resources associated with group connection */
/* FIXME: this function is likely to be broken and may crash/deadlock 
 * hopefully we don't need it for the demo */
/* there still can be races between gcs_close() and repl/recv functions,
 * gcs_send() locks connection so it is safe. */
int gcs_close (gcs_conn_t **gcs)
{
    int         err;
    gcs_conn_t *conn = *gcs;
    gcs_act_t  *act;

    if (!(gu_mutex_lock (&conn->lock)))
    {
        if (GCS_CONN_CLOSED == conn->state)
        {
            gu_mutex_unlock (&conn->lock);
            return -ENOTCONN;
        }
        conn->state = GCS_CONN_CLOSED;
        conn->err   = -ENOTCONN;
        /* we must unlock the mutex here to allow unfortunate threads
         * to acquire the lock and give up gracefully */
        gu_mutex_unlock (&conn->lock);
    }
    else {
        return -EBADFD;
    }

    /* abort all pending repl calls */
    /* destroying repl_lock will prevent repl threads from queueing */
    gu_debug ("Destroying repl_lock");
    while (gu_mutex_destroy (&conn->repl_lock)) {
        if ((err = gu_mutex_lock (&conn->repl_lock))) return -err;
        gu_mutex_unlock (&conn->repl_lock);
    }

    /* now that recv thread is cancelled, and no repl requests can be queued,
     * we can safely destroy repl_q */
    gu_debug ("Waking up repl threads");
    while ((act = gcs_fifo_get(conn->repl_q))) {
        /* This will wake up repl threads in repl_q - 
         * they'll quit on their own,
         * they don't depend on the connection object after waking */
        gu_cond_signal (&act->wait_cond);
    }
    gu_debug ("Destroying repl_q");
    if ((err = gcs_fifo_destroy (&conn->repl_q))) return err;

    /* this should cancel all recv calls */
    gu_debug ("Destroying recv_q");
    if ((err = gcs_queue_free (&conn->recv_q))) return err;

    /** @note: We have to close connection before joining RECV thread
     *         since it may be blocked in gcs_backend_receive().
     *         That also means, that RECV thread should surely exit here. */
    if ((err = gcs_generic_close (&conn->generic))) return err;


    //  /* Cancel main recv_thread if it is not blocked in gcs_backend_recv() */
    //  gu_thread_cancel (conn->recv_thread);
    //  gu_debug ("recv_thread() cancelled.");
    gu_thread_join   (conn->recv_thread, NULL);
    gu_debug ("recv_thread() joined.");

    /* This must not last for long */
    while (gu_mutex_destroy (&conn->lock));
    
    gu_free (conn);
    *gcs = NULL;

    return 0;
}

/* Puts action in the send queue and returns */
int gcs_send (gcs_conn_t *conn, const gcs_act_type_t act_type,
	      const size_t act_size, const uint8_t *action)
{
    int ret = -ENOTCONN;

    /*! locking connection here to avoid race with gcs_close()
     *  @note: gcs_repl() and gcs_recv() cannot lock connection
     *         because they block indefinitely waiting for actions */
    if (!gu_mutex_lock (&conn->lock)) { 
        if (GCS_CONN_OPEN == conn->state) {
            ret = gcs_generic_send (conn->generic, action, act_size, act_type);
        }
        gu_mutex_unlock (&conn->lock);
    }
    return ret;
}

/* Puts action in the send queue and returns after it is replicated */
int gcs_repl (gcs_conn_t          *conn,
	      const gcs_act_type_t act_type,
	      const size_t         act_size,
	      uint8_t             *action,
	      gcs_seqno_t         *act_id, 
	      gcs_seqno_t         *local_act_id)
{
    long ret;

    /* This is good - we don't have to do malloc because we wait */
    gcs_act_t act = { .act_size     = act_size,
		      .act_type     = act_type,
		      .act_id       = GCS_SEQNO_ILL,
		      .local_act_id = GCS_SEQNO_ILL,
		      .action       = action };

    *act_id       = GCS_SEQNO_ILL;
    *local_act_id = GCS_SEQNO_ILL;

    if (GCS_CONN_CLOSED == conn->state) return -ENOTCONN;

    assert (act_size > 0); // FIXME!!! see recv_thread() -
    assert (action);       // cannot gcs_repl() NULL messages

    gu_mutex_init (&act.wait_mutex, NULL);
    gu_cond_init  (&act.wait_cond,  NULL);

    /* Send action and wait for signal from recv_thread
     * we need to lock a mutex before we can go wait for signal */
    if (!(ret = gu_mutex_lock (&act.wait_mutex)))
    {
        /* make sure it is put in fifo and sent in the same order -
         * lock repl_lock */
        if (!(ret = gu_mutex_lock (&conn->repl_lock)))
        {
            if (!(ret = gcs_fifo_put (conn->repl_q, &act)))
            {
                ret = gcs_generic_send (conn->generic,
                                        action,
                                        act_size,
                                        act_type);
                if (ret < 0) {
                    /* sending failed - remove item from the queue */
                    if (gcs_fifo_remove(conn->repl_q)) {
                        gu_fatal ("Failed to recover repl_q");
                        ret = -ENOTRECOVERABLE;
                    }
                }
            }
            gu_mutex_unlock (&conn->repl_lock);
        }

        /* now having unlocked repl_q we can go waiting for action delivery */
        if (ret >= 0 && conn->state == GCS_CONN_OPEN) {
            gu_cond_wait (&act.wait_cond, &act.wait_mutex);
            if (act.act_id != GCS_SEQNO_ILL) {
                *act_id       = act.act_id;       /* set by recv_thread */
                *local_act_id = act.local_act_id; /* set by recv_thread */
            }
            else {
                /* action was not replicated for some reason */
                ret = -EINTR;
            }
        }
        gu_mutex_unlock  (&act.wait_mutex);
    }
    gu_mutex_destroy (&act.wait_mutex);
    gu_cond_destroy  (&act.wait_cond);

#ifdef GCS_GCS_DEBUG
//    printf ("act_size = %d\nact_type = %d\nact_id = %d\naction = %s\n",
//	    act.act_size, act.act_type, act.act_id, act.action);
#endif
    return ret;
}

/* Returns when an action from another process is received */
int gcs_recv (gcs_conn_t *conn, gcs_act_type_t *act_type,
	      size_t *act_size, uint8_t **action,
	      gcs_seqno_t *act_id, gcs_seqno_t *local_act_id)
{
    int        err;
    void      *void_ptr = NULL;
    gcs_act_t *act      = NULL;

    *act_size     = 0;
    *act_type     = GCS_ACT_UNKNOWN;
    *act_id       = GCS_SEQNO_ILL;
    *local_act_id = GCS_SEQNO_ILL;
    *action       = NULL;

    if (GCS_CONN_CLOSED == conn->state) return -ENOTCONN;

    if ((err = gcs_queue_pop_wait (conn->recv_q, &void_ptr)))
    {
	assert (NULL == void_ptr);
        if (GCS_CONN_CLOSED == conn->state) {
            gu_error ("gcs_recv() error: %d (%s)", err, gcs_strerror(err));
            return -ENOTCONN;
        } else {
            gu_error ("gcs_recv() error: %d (%s)", err, gcs_strerror(err));
            return err;
        }
    }

    act = void_ptr;
    *act_size     = act->act_size;
    *act_type     = act->act_type;
    *act_id       = act->act_id;
    *local_act_id = act->local_act_id;
    *action       = act->action;

    gu_free (act); /* was malloc'ed by recv_thread */

    return *act_size;
}

long
gcs_conf_set_pkt_size (gcs_conn_t *conn, long pkt_size)
{
    return gcs_generic_set_pkt_size (conn->generic, pkt_size);
}
