/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*
 * Top-level application interface implementation.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <galerautils.h>

#include "gcs.h"
#include "gcs_core.h"
#include "gcs_fifo_lite.h"
#include "gcs_queue.h"

const long GCS_MAX_REPL_THREADS = 16384;

typedef enum
{
    GCS_CONN_JOINED,
    GCS_CONN_OPEN,
    GCS_CONN_CLOSED,
    GCS_CONN_DESTROYED
}
gcs_conn_state_t;

struct gcs_fc
{
    uint32_t conf_id;
    uint32_t stop;
}
__attribute__((__packed__));

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
    gcs_fifo_lite_t*  repl_q;
    gu_thread_t  send_thread;

    /* A queue for threads waiting for received actions */
    gcs_queue_t* recv_q;
    gu_thread_t  recv_thread;

    /* Flow control */
    gu_mutex_t    fc_mutex;
    uint32_t      conf_id;     // configuration ID
    long          stop_sent;   // how many STOPs and CONTs were send
    long          stop_count;  // counts stop requests received
    volatile long queue_len;   // slave queue length
    volatile long upper_limit; // upper slave queue limit
    volatile long lower_limit; // lower slave queue limit

    /* gcs_core object */
    gcs_core_t  *core; // the context that is returned by
                       // the core group communication system
};

typedef struct gcs_act
{
    size_t         act_size;
    gcs_act_type_t act_type;
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    const void*    action;
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

                conn->repl_q = gcs_fifo_lite_create (GCS_MAX_REPL_THREADS,
                                                     sizeof (gcs_act_t*));
                if (conn->repl_q) {

                    conn->recv_q  = gcs_queue ();
                    if (conn->recv_q) {
                        gu_mutex_init (&conn->lock, NULL);
                        gu_mutex_init (&conn->fc_mutex, NULL);
                        conn->state = GCS_CONN_CLOSED;
                        return conn; // success
                    }
                    gcs_fifo_lite_destroy (conn->repl_q);
                }
            }
            gcs_core_destroy (conn->core);
        }

        gu_free (conn);
    }

    gu_error ("Failed to create GCS connection handle");

    return NULL; // failure
}

static inline long
gcs_fc_stop (gcs_conn_t* conn)
{
    long ret = 0;
    struct gcs_fc fc = { htogl(conn->conf_id), 1 };

    if (conn->stop_count > 0 || conn->stop_sent > 0 ||
        gcs_queue_length (conn->recv_q) <= conn->upper_limit ||
        GCS_CONN_JOINED != conn->state)
        return 0; // try to avoid mutex lock

    if (gu_mutex_lock (&conn->fc_mutex)) abort();

    conn->queue_len = gcs_queue_length (conn->recv_q);

    if ((conn->queue_len >  conn->upper_limit) &&
        GCS_CONN_JOINED  == conn->state        &&
        conn->stop_count <= 0 && conn->stop_sent <= 0) {
        /* tripped upper queue limit, send stop request */
        gu_info ("SENDING STOP (%llu)", conn->local_act_id); //track frequency
        ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));
        if (ret >= 0) {
            ret = 0;
            conn->stop_sent++;
        }
    }
//   gu_info ("queue_len = %ld, upper_limit = %ld, state = %d, stop_sent = %ld",
//            conn->queue_len, conn->upper_limit, conn->state, conn->stop_sent);
    gu_mutex_unlock (&conn->fc_mutex);

    return ret;
}

static inline long
gcs_fc_cont (gcs_conn_t* conn)
{
    long ret = 0;
    struct gcs_fc fc = { htogl(conn->conf_id), 0 };

    if (conn->stop_sent <= 0 ||
        gcs_queue_length (conn->recv_q) > conn->lower_limit ||
        GCS_CONN_JOINED != conn->state)
        return 0; // try to avoid mutex lock

    if (gu_mutex_lock (&conn->fc_mutex)) abort();

    conn->queue_len = gcs_queue_length (conn->recv_q);

    if (conn->lower_limit >= conn->queue_len &&
        GCS_CONN_JOINED == conn->state && conn->stop_sent > 0) {
        // tripped lower slave queue limit, sent continue request
        gu_info ("SENDING CONT");
        ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));
        if (ret >= 0) {
            ret = 0;
            conn->stop_sent--;
        }
    }
    gu_mutex_unlock (&conn->fc_mutex);

    return ret;
}

/*!
 * Performs work requred by action in current context.
 * @return negative error code, 1 if action should be discarded, 0 if should be
 *         passed to application.
 */
static long
gcs_handle_actions (gcs_conn_t*    conn,
                    const void*    action,
                    size_t         act_size,
                    gcs_act_type_t act_type)
{
    long ret = 1;

    switch (act_type) {
    case GCS_ACT_CONF:
    {
        const gcs_act_conf_t* conf = action;

        if ((conn->conf_id + 1) != conf->conf_id) {
            /* missed a configuration, need a snapshot - no longer JOINED */
// temporary            if (conn->state == GCS_CONN_JOINED) conn->state = GCS_CONN_OPEN;
        }

        if (gu_mutex_lock (&conn->fc_mutex)) abort();
        {
            conn->conf_id     = conf->conf_id;
            conn->stop_sent   = 0;
            conn->stop_count  = 0;
            conn->lower_limit = 2 * conf->memb_num;
            conn->upper_limit = 2 * conn->lower_limit;
        }
        gu_mutex_unlock (&conn->fc_mutex);
        ret = 0;
        break;
    }
    case GCS_ACT_FLOW:
    {
        const struct gcs_fc* fc = action;
        assert (sizeof(*fc) == act_size);
// UNCOMMENT WHEN SYNC MESSAGE IS IMPLEMENTED        if (gtohl(fc->conf_id) != conn->conf_id) break; // obsolete fc request
        gu_info ("RECEIVED %s", fc->stop ? "STOP" : "CONT");
        conn->stop_count += ((fc->stop != 0) << 1) - 1; // +1 if !0, -1 if 0
        break;
    }
    default:
        break;
    }

    return ret;
}

/*
 * gcs_recv_thread() receives whatever actions arrive from group,
 * and performs necessary actions based on action type.
 */
static void *gcs_recv_thread (void *arg)
{
    long           ret  = -ECONNABORTED;
    gcs_conn_t*    conn = arg;
    gcs_act_t*     act;
    gcs_act_t**    act_ptr;
    gcs_seqno_t    act_id;
    gcs_act_type_t act_type;
    ssize_t        act_size;
    const void*    action;
    const void*    local_action = NULL;

//    gu_debug ("Starting RECV thread");

    // To avoid race between gcs_open() and the following state check in while()
    // (I'm trying to avoid mutex locking in this loop, so lock/unlock before)
    gu_mutex_lock   (&conn->lock); // wait till gcs_open() is done;
    gu_mutex_unlock (&conn->lock);

    while (conn->state <= GCS_CONN_OPEN)
    {
        gcs_seqno_t this_act_id = GCS_SEQNO_ILL;

	if ((act_size = gcs_core_recv (conn->core,
                                       &action,
                                       &act_type,
                                       &act_id)) < 0) {
	    ret = act_size;
	    gu_debug ("gcs_core_recv returned %d: %s", ret, strerror(-ret));
	    break;
	}

//        gu_info ("Received action type: %d, size: %d, global seqno: %lld",
//                 act_type, act_size, (long long)act_id);

        if (act_type >= GCS_ACT_CONF) {
            ret = gcs_handle_actions (conn, action, act_size, act_type);
            if (ret < 0) {         // error
                gu_debug ("gcs_handle_actions returned %d: %s",
                          ret, strerror(-ret));
                break;
            }
            if (ret > 0) continue; // not for application
        }

        /* deliver to application and increment local order */
        this_act_id = ++conn->local_act_id;

	if (!local_action && (act_ptr = gcs_fifo_lite_get_head(conn->repl_q))) {
	    /* Check if there is any local action in REPL queue head */
            /* local_action will be used to determine, whether we deliver
             * action to REPL application thread or to RECV application thread*/
            local_action = (*act_ptr)->action;
            assert (local_action != NULL);
            gcs_fifo_lite_release (conn->repl_q);
        }

	/* Since this is the only way, NULL-sized actions cannot be REPL'ed */
	if (local_action          /* there is REPL thread waiting for action */
            &&
            action == local_action) /* this is the action it is waiting for */
	{
	    /* local action */
//            gu_debug ("Local action");
	    if ((act_ptr = gcs_fifo_lite_get_head (conn->repl_q))) {
                act = *act_ptr;
                gcs_fifo_lite_pop_head (conn->repl_q);

                assert (act->action   == action);
                assert (act->act_size == act_size);

                act->act_id       = act_id;
                act->local_act_id = this_act_id;

                gu_mutex_lock   (&act->wait_mutex);
                gu_cond_signal  (&act->wait_cond);
                gu_mutex_unlock (&act->wait_mutex);

                local_action = NULL;
            }
            else {
                gu_fatal ("Failed to get local action pointer");
                assert (0);
                abort();
            }
	}
	else
	{
	    /* slave action */
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
            act->local_act_id = this_act_id;
	    act->action       = action;

	    if ((ret = gcs_queue_push (conn->recv_q, act)) < 0) {
                gu_debug ("gcs_queue_push() returned %d: %s",
                          ret, strerror (-ret));
                break;
            }

            // Send FC_STOP if it is necessary
            if ((ret = gcs_fc_stop(conn))) {
                gu_debug ("gcs_fc_stop() returned %d: %s", ret, strerror(-ret));
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
    
    if (ret > 0) ret = 0;
    gu_debug ("RECV thread exiting %d: %s", ret, strerror(-ret));
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
                    gu_info ("Opened channel '%s'", channel);
                }
                else {
                    gcs_core_close (conn->core);
                }
            }
            else {
                gu_error ("Failed to open channel '%s': %d (%s)",
                          channel, ret, strerror(-ret));
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

    if ((ret = gu_mutex_lock (&conn->lock))) return ret;
    {
        gcs_act_t** act_ptr;

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
        gcs_fifo_lite_close (conn->repl_q);
        while ((act_ptr = gcs_fifo_lite_get_head (conn->repl_q))) {
            gcs_act_t* act = *act_ptr;
            gcs_fifo_lite_pop_head (conn->repl_q);

            /* This will wake up repl threads in repl_q - 
             * they'll quit on their own,
             * they don't depend on the conn object after waking */
            gu_mutex_lock   (&act->wait_mutex);
            gu_cond_signal  (&act->wait_cond);
            gu_mutex_unlock (&act->wait_mutex);
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
                gu_error ("Attempt to call gcs_destroy() before gcs_close(): "
                          "state = %d", conn->state);
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

    if ((err = gcs_fifo_lite_destroy (conn->repl_q))) {
        gu_debug ("Error destroying repl FIFO: %d (%s)", err, strerror(-err));
        return err;
    }

    /* this should cancel all recv calls */
    if ((err = gcs_queue_free (conn->recv_q))) {
        gu_debug ("Error destroying recv queue: %d (%s)", err, strerror(-err));
        return err;
    }

    if ((err = gcs_core_destroy (conn->core))) {
        gu_debug ("Error destroying core: %d (%s)", err, strerror(-err));
        return err;
    }

    /* This must not last for long */
    while (gu_mutex_destroy (&conn->lock));
    while (gu_mutex_destroy (&conn->fc_mutex));
    
    gu_free (conn);

    return 0;
}

/* Puts action in the send queue and returns */
int gcs_send (gcs_conn_t*          conn,
              const void*          action,
	      const size_t         act_size,
              const gcs_act_type_t act_type)
{
    int ret = -ENOTCONN;

    /*! locking connection here to avoid race with gcs_close()
     *  @note: gcs_repl() and gcs_recv() cannot lock connection
     *         because they block indefinitely waiting for actions */
    if (!(ret = gu_mutex_lock (&conn->lock))) { 
        if (GCS_CONN_OPEN >= conn->state) {
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
	      const void          *action,
	      const size_t         act_size,
	      const gcs_act_type_t act_type,
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
            gcs_act_t** act_ptr;
            // some hack here to achieve one if() instead of two:
            // if (conn->state != GCS_CONN_JOINED) ret will be -ENOTCONN
            ret = -ENOTCONN;
            if ((GCS_CONN_OPEN >= conn->state) &&
                (act_ptr = gcs_fifo_lite_get_tail (conn->repl_q)))
            {
                *act_ptr = &act;
                gcs_fifo_lite_push_tail (conn->repl_q);
                // Keep on trying until something else comes out
                while ((ret = gcs_core_send (conn->core, action,
                                             act_size, act_type)) == -ERESTART);
                if (ret < 0) {
                    /* sending failed - remove item from the queue */
                    gu_warn ("Send action returned %d (%s)",
                             ret, strerror(-ret));
                    if (gcs_fifo_lite_remove (conn->repl_q)) {
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
        assert(ret);
        /* now having unlocked repl_q we can go waiting for action delivery */
        if (ret >= 0 && GCS_CONN_OPEN >= conn->state) {
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
//    gu_debug ("\nact_size = %u\nact_type = %u\n"
//              "act_id   = %llu\naction   = %p (%s)\n",
//              act.act_size, act.act_type, act.act_id, act.action, act.action);
#endif
    return ret;
}

/* Returns when an action from another process is received */
int gcs_recv (gcs_conn_t*     conn,
              void**          action,
	      size_t*         act_size,
              gcs_act_type_t* act_type,
	      gcs_seqno_t*    act_id,
              gcs_seqno_t*    local_act_id)
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

    if ((err = gcs_queue_pop_wait (conn->recv_q, &void_ptr)) < 0)
    {
	assert (NULL == void_ptr);
        if (GCS_CONN_CLOSED == conn->state) {
            // Almost any error (like ENODATA or ENOTCONN) at this point
            // is probably Ok
            if (-ENOTRECOVERABLE != err) {
                return -ENOTCONN;
            }
            else {
                gu_error ("gcs_recv() error: %d (%s)", err, strerror(-err));
                return err;
            }
        } else {
            gu_error ("gcs_recv() error: %d (%s)", err, strerror(-err));
            return err;
        }
    }

    if ((err = gcs_fc_cont(conn))) return err;

    act = void_ptr;
    *act_size     = act->act_size;
    *act_type     = act->act_type;
    *act_id       = act->act_id;
    *local_act_id = act->local_act_id;
    *action       = (uint8_t*)act->action;

    gu_free (act); /* was malloc'ed by recv_thread */

    return *act_size;
}

int
gcs_wait (gcs_conn_t* conn)
{
    if (gu_likely(GCS_CONN_OPEN >= conn->state)) {
       return (conn->stop_count > 0 || (conn->queue_len > conn->upper_limit));
    }
//    if (gu_likely(GCS_CONN_OPEN >= conn->state)) {
//        return (conn->stop_count > 0);
//    }
    else {
        switch (conn->state) {
        case GCS_CONN_CLOSED:
            return -ENOTCONN;
        case GCS_CONN_DESTROYED:
        default:
            return -EBADFD;
        }
    }
}

int
gcs_join (gcs_conn_t* conn)
{
    int ret;

    if (!(ret = gu_mutex_lock (&conn->lock)))
    {
        conn->state = GCS_CONN_JOINED;
        gu_info ("JOINED");
        gu_mutex_unlock (&conn->lock);
    }
    return ret;
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

