/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * Top-level application interface implementation.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <galerautils.h>

#define GCS_FIFO_SAFE

#include "gcs.h"
#include "gcs_core.h"
#include "gcs_fifo.h"
#include "gcs_queue.h"

const long GCS_MAX_REPL_THREADS = 16384;

typedef enum
{
    GCS_CONN_OPEN,
    GCS_CONN_CLOSED,
    GCS_CONN_DESTROYED
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

    /* A queue for threads waiting for received actions */
    gcs_queue_t* recv_q;
    gu_thread_t  recv_thread;

//    /* A condition for threads which called gcs_recv()  */
//    gu_cond_t *recv_cond;

    gcs_core_t  *core; // the context that is returned by
                       // the core group communication system
};

typedef struct gcs_act
{
    size_t         act_size;
    gcs_act_type_t act_type;
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    const uint8_t* action;
    gu_mutex_t     wait_mutex;
    gu_cond_t      wait_cond;
}
gcs_act_t;

/* Creates a group connection handle */
gcs_conn_t*
gcs_create (const char *backend)
{
    gcs_conn_t *conn = GU_CALLOC (1, gcs_conn_t);

    if (conn) {
        conn->state = GCS_CONN_DESTROYED;

        conn->core = gcs_core_create (backend);
        if (conn->core) {

            if (!(gcs_core_set_pkt_size (conn->core, GCS_DEFAULT_PKT_SIZE))) {

                conn->repl_q  = GCS_FIFO_CREATE (GCS_MAX_REPL_THREADS);
                if (conn->repl_q) {

                    conn->recv_q  = gcs_queue ();
                    if (conn->recv_q) {
                        gu_mutex_init (&conn->lock, NULL);
                        conn->state = GCS_CONN_CLOSED;
                        return conn; // success
                    }
                    GCS_FIFO_DESTROY (&conn->repl_q);
                }
            }
            gcs_core_destroy (conn->core);
        }

        gu_free (conn);
    }

    gu_error ("Failed to create GCS connection handle");

    return NULL; // failure
}

/*
 * gcs_recv_thread() receives whatever actions arrive from group,
 * and performs necessary actions based on action type.
 */
static void *gcs_recv_thread (void *arg)
{
    long           ret  = -ECONNABORTED;
    gcs_conn_t    *conn = arg;
    gcs_act_t     *act  = NULL;
    gcs_seqno_t    act_id;
    gcs_act_type_t act_type;
    ssize_t        act_size;
    uint8_t       *action;
    const uint8_t *action_repl = NULL;

//    gu_debug ("Starting RECV thread");

    // To avoid race between gcs_open() and the following state check in while()
    // (I'm trying to avoid mutex locking in this loop, so lock/unlock before)
    gu_mutex_lock   (&conn->lock); // wait till gcs_open() is done;
    gu_mutex_unlock (&conn->lock);

    while (conn->state == GCS_CONN_OPEN)
    {
        gcs_seqno_t this_act_id = GCS_SEQNO_ILL;

	if ((act_size = gcs_core_recv (conn->core,
                                       &action,
                                       &act_type,
                                       &act_id)) < 0) {
	    ret = act_size;
	    gu_debug ("gcs_core_recv returned %d: %s", ret, gcs_strerror(ret));
	    break;
	}

        if (act_type < GCS_ACT_SNAPSHOT) {
	    /* deliver to application and increment local order */
	    this_act_id = ++conn->local_act_id;
        }

//        gu_info ("Received action type: %d, size: %6d, #%llu",
//                 act_type, act_size, this_act_id);

	if (!action_repl) {
	    /* Check if there is any local repl action in queue */
	    act = GCS_FIFO_HEAD (conn->repl_q);
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
	    act = GCS_FIFO_GET (conn->repl_q);
	    
//	    act->act_id       = act_id;
	    act->act_id       = this_act_id;
	    act->local_act_id = this_act_id;
	    assert (act->action   == action);
            assert (act->act_size == act_size);

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
//	    act->act_id       = act_id;
	    act->act_id       = this_act_id;
            act->local_act_id = this_act_id;
	    act->action       = action;

	    if ((ret = gcs_queue_push (conn->recv_q, act))) {
		break;
            }
	    act = NULL;
//            gu_info("Received foreign action of type %d, size %d, id=%llu, "
//                    "action %p", act_type, act_size, this_act_id, action);
	}
        // below I can't use any references to act
//       gu_debug("Received action of type %d, size %d, id=%llu, local id=%llu",
//                 act_type, act_size, act_id, conn->local_act_id);
    }
    
    gu_debug ("RECV thread exiting %d: %s", ret, gcs_strerror(ret));
    return NULL;
}

/* Opens connection to group */
int gcs_open (gcs_conn_t *conn, const char *channel)
{
    long ret = 0;

    if ((ret = gcs_queue_reset (conn->recv_q))) return ret;

    if ((ret = gu_mutex_lock (&conn->lock))) return ret;
    {
        if (GCS_CONN_CLOSED == conn->state) {

            if (!(ret = gcs_core_open (conn->core, channel))) {

                if (!(ret = gu_thread_create (&conn->recv_thread,
                                              NULL,
                                              gcs_recv_thread,
                                              conn))) {
                    conn->state = GCS_CONN_OPEN;
                    gu_info ("Joined channel '%s'", channel);
                }
                else {
                    gcs_core_close (conn->core);
                }
            }
            else {
                gu_error ("Failed to join channel '%s': %d (%s)",
                          channel, ret, gcs_strerror(ret));
            }
        }
        else {
            ret = -EBADFD;
        }
    }

    gu_mutex_unlock (&conn->lock);
    return ret;
}

/* Closes group connection */
/* After it returns, application should have all time in the world to cancel
 * and join threads which try to access the handle, before calling gcs_destroy()
 * on it. */
int gcs_close (gcs_conn_t *conn)
{
    long        ret;
    gcs_act_t  *act;

    if ((ret = gu_mutex_lock (&conn->lock))) return ret;
    {
        if (GCS_CONN_CLOSED <= conn->state)
        {
            gu_mutex_unlock (&conn->lock);
            return -EBADFD;
        }
        conn->state = GCS_CONN_CLOSED;
        conn->err   = -ECONNABORTED;

        /** @note: We have to close connection before joining RECV thread
         *         since it may be blocked in gcs_backend_recv().
         *         That also means, that RECV thread should surely exit here. */
        if ((ret = gcs_core_close (conn->core))) {
            return ret;
        }

        gu_thread_join (conn->recv_thread, NULL);
        gu_debug ("recv_thread() joined.");

        /* At this point (state == CLOSED) no new threads should be able to
         * queue for repl (check gcs_repl()), and recv thread is joined, so no
         * new actions will be received. Abort threads that are still waiting
         * in repl queue */
        GCS_FIFO_CLOSE(conn->repl_q); // hack to avoid hanging in empty queue
        while ((act = GCS_FIFO_GET(conn->repl_q))) {
            /* This will wake up repl threads in repl_q - 
             * they'll quit on their own,
             * they don't depend on the connection object after waking */
            gu_cond_signal (&act->wait_cond);
        }

        /* wake all gcs_recv() threads */
        ret = gcs_queue_abort (conn->recv_q);
    }
    gu_mutex_unlock (&conn->lock);
    return ret;
}

/* Frees resources associated with GCS connection handle */
int gcs_destroy (gcs_conn_t *conn)
{
    int         err;

    if (!(err = gu_mutex_lock (&conn->lock)))
    {
        if (GCS_CONN_CLOSED != conn->state)
        {
            if (GCS_CONN_CLOSED > conn->state)
                gu_error ("Attempt to call gcs_destroy() before gcs_close()");
            gu_mutex_unlock (&conn->lock);
            return -EBADFD;
        }
        conn->state = GCS_CONN_DESTROYED;
        conn->err   = -EBADFD;
        /* we must unlock the mutex here to allow unfortunate threads
         * to acquire the lock and give up gracefully */
        gu_mutex_unlock (&conn->lock);
    }
    else {
        return err;
    }

    if ((err = GCS_FIFO_DESTROY (&conn->repl_q))) {
        gu_debug ("Error destroying repl FIFO: %d (%s)",
                  err, gcs_strerror (err));
        return err;
    }

    /* this should cancel all recv calls */
    if ((err = gcs_queue_free (conn->recv_q))) {
        gu_debug ("Error destroying recv queue: %d (%s)",
                  err, gcs_strerror (err));
        return err;
    }

    if ((err = gcs_core_destroy (conn->core))) {
        gu_debug ("Error destroying core: %d (%s)",
                  err, gcs_strerror (err));
        return err;
    }

    /* This must not last for long */
    while (gu_mutex_destroy (&conn->lock));
    
    gu_free (conn);

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
    if (!(ret = gu_mutex_lock (&conn->lock))) { 
        if (GCS_CONN_OPEN == conn->state) {
            /* need to make a copy of the action, since receiving thread
             * has no way of knowing that it shares this buffer.
             * also the contents of action may be changed afterwards by
             * the sending thread */
            void* act = malloc (act_size);
            if (act != NULL) {
                memcpy (act, action, act_size);
                while ((ret = gcs_core_send (conn->core, act,
                                             act_size, act_type)) == -ERESTART);
            }
            else {
                ret = -ENOMEM;
            }
        }
        gu_mutex_unlock (&conn->lock);
    }
    return ret;
}

/* Puts action in the send queue and returns after it is replicated */
int gcs_repl (gcs_conn_t          *conn,
	      const gcs_act_type_t act_type,
	      const size_t         act_size,
	      const uint8_t       *action,
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

    assert (act_size > 0); // FIXME!!! see recv_thread() -
    assert (action);       // cannot gcs_repl() NULL messages
    assert (act.action);

    gu_mutex_init (&act.wait_mutex, NULL);
    gu_cond_init  (&act.wait_cond,  NULL);

    /* Send action and wait for signal from recv_thread
     * we need to lock a mutex before we can go wait for signal */
    if (!(ret = gu_mutex_lock (&act.wait_mutex)))
    {
        // Lock here does the following:
        // 1. serializes gcs_core_send() access between gcs_repl() and
        //    gcs_send()
        // 2. avoids race with gcs_close() and gcs_destroy()
        if (!(ret = gu_mutex_lock (&conn->lock)))
        {
            // some hack here to achieve one if() instead of two:
            // if (conn->state != GCS_CONN_OPEN) ret will be -EBADFD,
            // otherwise it will be set to what gcs_fifo_put() returns.
            ret = -EBADFD;
            if ((GCS_CONN_OPEN == conn->state) &&
                !(ret = GCS_FIFO_PUT (conn->repl_q, &act)))
            {
                // Keep on trying until something else comes out
                while ((ret = gcs_core_send (conn->core, action,
                                             act_size, act_type)) == -ERESTART);
                if (ret < 0) {
                    /* sending failed - remove item from the queue */
                    if (GCS_FIFO_REMOVE (conn->repl_q)) {
                        gu_fatal ("Failed to recover repl_q");
                        ret = -ENOTRECOVERABLE;
                    }
                }
                else {
                    assert (ret == act_size);
                }
            }
            gu_mutex_unlock (&conn->lock);
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

#ifdef GCS_DEBUG_GCS
    gu_debug ("\nact_size = %u\nact_type = %u\n"
              "act_id   = %llu\naction   = %p (%s)\n",
              act.act_size, act.act_type, act.act_id, act.action, act.action);
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
            // Almost any error (like ENODATA or ENOTCONN) at this point
            // is probably Ok
            if (-ENOTRECOVERABLE != err) {
                return -ENOTCONN;
            }
            else {
                gu_error ("gcs_recv() error: %d (%s)", err, gcs_strerror(err));
                return err;
            }
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
    *action       = (uint8_t*)act->action;

    gu_free (act); /* was malloc'ed by recv_thread */

    return *act_size;
}

long
gcs_conf_set_pkt_size (gcs_conn_t *conn, long pkt_size)
{
    return gcs_core_set_pkt_size (conn->core, pkt_size);
}

long
gcs_set_last_applied (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    return gcs_core_set_last_applied (conn->core, seqno);
}

