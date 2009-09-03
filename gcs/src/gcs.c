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

const long GCS_MAX_REPL_THREADS = 16384;

const long base_queue_limit = 4;

typedef enum
{
    GCS_CONN_SYNCED,   // caught up with the rest of the group
    GCS_CONN_JOINED,   // state transfer complete
    GCS_CONN_DONOR,    // in state transfer, donor
    GCS_CONN_JOINER,   // in state transfer, joiner
    GCS_CONN_OPEN,     // just connected to group, require state transfer
    GCS_CONN_CLOSED,
    GCS_CONN_DESTROYED
}
gcs_conn_state_t;

#define GCS_CLOSED_ERROR -EBADFD; // file descriptor in bad state

static const char* gcs_conn_state_string[GCS_CONN_DESTROYED + 1] =
{
    "SYNCED",
    "JOINED",
    "DONOR",
    "JOINER",
    "OPEN",
    "CLOSED",
    "DESTROYED"
};

/** Flow control message */
struct gcs_fc
{
    uint32_t conf_id; // least significant part of configuraiton seqno
    uint32_t stop;    // boolean value
}
__attribute__((__packed__));

struct gcs_conn
{
    int    my_idx;
    char  *my_name;
    char  *channel;
    char  *socket;

    gcs_conn_state_t state;
    int err;
    gu_mutex_t   lock;

    gcs_seqno_t  local_act_id; /* local seqno of the action  */

    /* A queue for threads waiting for replicated actions */
    gcs_fifo_lite_t*  repl_q;
    gu_thread_t  send_thread;

    /* A queue for threads waiting for received actions */
    gu_fifo_t*   recv_q;
    gu_thread_t  recv_thread;

    /* Flow control */
//    gu_mutex_t    fc_mutex;
    uint32_t     conf_id;     // configuration ID
    long         stop_sent;   // how many STOPs and CONTs were send
    long         stop_count;  // counts stop requests received
    long queue_len;   // slave queue length
    long upper_limit; // upper slave queue limit
    long lower_limit; // lower slave queue limit

    /* gcs_core object */
    gcs_core_t  *core; // the context that is returned by
                       // the core group communication system
};

typedef struct gcs_act
{
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    const void*    action;
    ssize_t        act_size;
    gcs_act_type_t act_type;
    gu_mutex_t     wait_mutex;
    gu_cond_t      wait_cond;
}
gcs_act_t;

typedef struct gcs_slave_act
{
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    const void*    action;
    size_t         act_size;
    gcs_act_type_t act_type;
}
gcs_slave_act_t;

/* Creates a group connection handle */
gcs_conn_t*
gcs_create (const char* node_name, const char* inc_addr)
{
    gcs_conn_t* conn = GU_CALLOC (1, gcs_conn_t);

    if (conn) {
        conn->state = GCS_CONN_DESTROYED;
        conn->core  = gcs_core_create (node_name, inc_addr);

        if (conn->core) {
            conn->repl_q = gcs_fifo_lite_create (GCS_MAX_REPL_THREADS,
                                                 sizeof (gcs_act_t*));

            if (conn->repl_q) {
                size_t recv_q_len = GU_AVPHYS_PAGES * GU_PAGE_SIZE /
                                    sizeof(gcs_slave_act_t) / 4;
                gu_debug ("Requesting recv queue len: %zu", recv_q_len);
                conn->recv_q = gu_fifo_create (recv_q_len,
                                               sizeof(gcs_slave_act_t));

                if (conn->recv_q) {
                    conn->state        = GCS_CONN_CLOSED;
                    conn->my_idx       = -1;
                    conn->local_act_id = GCS_SEQNO_FIRST;
                    return conn; // success
                }
                else {
                    gu_error ("Failed to create recv_q.");
                }

                gcs_fifo_lite_destroy (conn->repl_q);
            }
            else {
                gu_error ("Failed to create repl_q.");
            }

            gcs_core_destroy (conn->core);
        }
        else {
            gu_error ("Failed to create core.");
        }
        gu_free (conn);
    }
    else {
        gu_error ("Could not allocate GCS connection handle: %s",
                  strerror (ENOMEM));
    }
    gu_error ("Failed to create GCS connection handle.");

    return NULL; // failure
}

long
gcs_init (gcs_conn_t* conn, gcs_seqno_t seqno, const uint8_t uuid[GU_UUID_LEN])
{
    if (conn->state == GCS_CONN_CLOSED) {
        return gcs_core_init (conn->core, seqno, (const gu_uuid_t*)uuid);
    }
    else {
        gu_error ("State must be CLOSED");
        if (conn->state < GCS_CONN_CLOSED)
            return -EBUSY;
        else // DESTROYED
            return -EBADFD;
    }
}

static inline long
gcs_fc_stop (gcs_conn_t* conn)
{
    long ret = 0;
    struct gcs_fc fc = { htogl(conn->conf_id), 1 };

    conn->queue_len = gu_fifo_length (conn->recv_q);

    if ((conn->queue_len >  conn->upper_limit) &&
        GCS_CONN_SYNCED  == conn->state        &&
        conn->stop_count <= 0 && conn->stop_sent <= 0) {
        /* tripped upper slave queue limit: send stop request */
        gu_debug ("SENDING STOP (%llu)", conn->local_act_id); //track frequency
        ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));
        if (ret >= 0) {
            ret = 0;
            conn->stop_sent++;
        }
    }
//   gu_info ("queue_len = %ld, upper_limit = %ld, state = %d, stop_sent = %ld",
//            conn->queue_len, conn->upper_limit, conn->state, conn->stop_sent);

    return ret;
}

static inline long
gcs_fc_cont (gcs_conn_t* conn)
{
    long ret = 0;
    struct gcs_fc fc = { htogl(conn->conf_id), 0 };

    assert (GCS_CONN_SYNCED == conn->state);

    conn->queue_len = gu_fifo_length (conn->recv_q);

    if (conn->lower_limit >= conn->queue_len &&  conn->stop_sent > 0) {
        // tripped lower slave queue limit: send continue request
        gu_debug ("SENDING CONT");
        ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));
        if (ret >= 0) {
            ret = 0;
            conn->stop_sent--;
        }
    }

    return ret;
}

static inline long
gcs_send_sync (gcs_conn_t* conn) {
    long ret = 0;

    assert (GCS_CONN_JOINED == conn->state);

    conn->queue_len = gu_fifo_length (conn->recv_q);

    if (conn->lower_limit >= conn->queue_len) {
        // tripped lower slave queue limit, send SYNC message
        gu_debug ("SENDING SYNC");
        ret = gcs_core_send_sync (conn->core, 0);
        if (ret >= 0) {
            ret = 0;
        }
    }

    return ret;
}

/*!
 * State transition functions - just in case we want to add something there.
 */
static void
gcs_become_open (gcs_conn_t* conn)
{
    if (conn->state < GCS_CONN_OPEN) conn->state = GCS_CONN_OPEN;
}

static void
gcs_become_joiner (gcs_conn_t* conn)
{
    if (conn->state == GCS_CONN_OPEN) conn->state = GCS_CONN_JOINER;
}

// returns 1 if accepts, 0 if rejects, negative error code if fails.
static long
gcs_become_donor (gcs_conn_t* conn)
{
    if (conn->state <= GCS_CONN_JOINED) {
        conn->state = GCS_CONN_DONOR;
        return 1;
    }
    else if (conn->state < GCS_CONN_OPEN){
        ssize_t err;
        gu_warn ("Received State Transfer Request in wrong state %s. "
                 "Rejecting.", gcs_conn_state_string[conn->state]);
        // reject the request.
        // error handling currently is way too simplistic
        err = gcs_join (conn, -EPROTO);
        if (err < 0 && !(err == -ENOTCONN || err == -EBADFD)) {
            gu_fatal ("Failed to send State Transfer Request rejection: "
                      "%zd (%s)", err, (strerror (-err)));
            assert (0);
            return -ENOTRECOVERABLE; // failed to clear donor status,
        }
    }
    return 0; // do not pass to application
}

static void
gcs_become_joined (gcs_conn_t* conn)
{
    if (GCS_CONN_JOINER == conn->state ||
        GCS_CONN_DONOR  == conn->state) {
        long ret;

        conn->state = GCS_CONN_JOINED;

        /* One of the cases when the node can become SYNCED */
        if ((ret = gcs_send_sync (conn))) {
            gu_warn ("Sending SYNC failed: %ld (%s)", ret, strerror (-ret));
        }
    }
    else if (conn->state < GCS_CONN_OPEN){
        gu_warn ("Received JOIN action in wrong state %s",
                 gcs_conn_state_string[conn->state]);
        assert (0);
    }
}

static void
gcs_become_synced (gcs_conn_t* conn)
{
    if (GCS_CONN_JOINED == conn->state) {
        conn->state = GCS_CONN_SYNCED;
    }
    else if (conn->state < GCS_CONN_OPEN && conn->state > GCS_CONN_SYNCED) {
        gu_warn ("Received SYNC action in wrong state %s",
                 gcs_conn_state_string[conn->state]);
        // assert (0); may happen 
    }
}

/*! Handles configuration action */
// TODO: this function does not provide any way for recv_thread to gracefully
//       exit in case of self-leave message.
static void
gcs_handle_act_conf (gcs_conn_t* conn, const void* action)
{
    const gcs_act_conf_t* conf = action;
    long ret;

    conn->my_idx = conf->my_idx;

    if (conf->conf_id < 0) {
        assert (conf->my_idx < 0);
        gu_info ("Received self-leave message. Closing connection.");
        conn->state = GCS_CONN_CLOSED;
        return;
    }

    if (conf->st_required) {
        gcs_become_open (conn);
    }
    else if (GCS_CONN_OPEN == conn->state) {
        /* if quorum decided that state transfer is not needed, we're as good
         * as joined. */
        conn->state = GCS_CONN_JOINED;
    }

    /* reset flow control as membership is most likely changed */
    gu_fifo_lock(conn->recv_q);
    {
        conn->conf_id     = conf->conf_id;
        conn->stop_sent   = 0;
        conn->stop_count  = 0;
        conn->lower_limit = base_queue_limit * sqrt(conf->memb_num);
        conn->upper_limit = 2 * conn->lower_limit;
    }
    gu_fifo_release (conn->recv_q);

    /* One of the cases when the node can become SYNCED */
    if (GCS_CONN_JOINED == conn->state && (ret = gcs_send_sync (conn))) {
        gu_warn ("Sending SYNC failed: %ld (%s)", ret, strerror (-ret));
    }
}

/*!
 * Performs work requred by action in current context.
 * @return negative error code, 0 if action should be discarded, 1 if should be
 *         passed to application.
 */
static long
gcs_handle_actions (gcs_conn_t*    conn,
                    const void*    action,
                    size_t         act_size,
                    gcs_act_type_t act_type,
                    gcs_seqno_t    act_id)
{
    long ret = 0;

    switch (act_type) {
    case GCS_ACT_FLOW:
    { // this is frequent, so leave it inlined.
        const struct gcs_fc* fc = action;
        assert (sizeof(*fc) == act_size);
        if (gtohl(fc->conf_id) != (uint32_t)conn->conf_id) {
            // obsolete fc request
            break;
        }
//        gu_info ("RECEIVED %s", fc->stop ? "STOP" : "CONT");
        conn->stop_count += ((fc->stop != 0) << 1) - 1; // +1 if !0, -1 if 0
        break;
    }
    case GCS_ACT_CONF:
        gcs_handle_act_conf (conn, action);
        ret = 1;
        break;
    case GCS_ACT_STATE_REQ:
        if ((gcs_seqno_t)conn->my_idx == act_id) {
            gu_info ("Got GCS_ACT_STATE_REQ to %lld, my idx: %ld",
                     act_id, conn->my_idx);
            ret = gcs_become_donor (conn);
            gu_info ("Becoming donor: %s", 1 == ret ? "yes" : "no");
        }
        else {
            if (act_id >= 0) gcs_become_joiner (conn);
            ret = 1; // pass to gcs_request_state_transfer() caller.
        }
        break; 
    case GCS_ACT_JOIN:
        gu_debug ("Got GCS_ACT_JOIN");
        gcs_become_joined (conn);
        break;
    case GCS_ACT_SYNC:
        gu_debug ("Got GCS_ACT_SYNC");
        gcs_become_synced (conn);
        break;
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
    long            ret  = -ECONNABORTED;
    gcs_conn_t*     conn = arg;
    gcs_act_t*      act;
    gcs_act_t**     act_ptr;
    gcs_seqno_t     act_id;
    gcs_act_type_t  act_type;
    ssize_t         act_size;
    const void*     action;
    const void*     local_action = NULL;

    // To avoid race between gcs_open() and the following state check in while()
    gu_mutex_lock   (&conn->lock);
    gu_mutex_unlock (&conn->lock);

    while (conn->state < GCS_CONN_CLOSED)
    {
        gcs_seqno_t this_act_id = GCS_SEQNO_ILL;

        act_size = gcs_core_recv (conn->core, &action, &act_type, &act_id);

	if (gu_unlikely(act_size <= 0)) {

            assert (GCS_ACT_ERROR == act_type);
            assert (GCS_SEQNO_ILL == act_id);
            assert (NULL == action);

	    gcs_act_t *slave_act    = gu_fifo_get_tail(conn->recv_q);
            slave_act->act_size     = 0;
            slave_act->act_type     = GCS_ACT_ERROR;
            slave_act->act_id       = GCS_SEQNO_ILL;
            slave_act->local_act_id = GCS_SEQNO_ILL;
            slave_act->action       = NULL;
            gu_fifo_push_tail(conn->recv_q);

            ret = act_size;
            gu_debug ("gcs_core_recv returned %d: %s", ret, strerror(-ret));
	    break;
	}

//        gu_info ("Received action type: %d, size: %d, global seqno: %lld",
//                 act_type, act_size, (long long)act_id);

        assert (act_type < GCS_ACT_ERROR);

        if (gu_unlikely(act_type >= GCS_ACT_STATE_REQ)) {
            ret = gcs_handle_actions (conn, action, act_size, act_type, act_id);
            if (ret < 0) {         // error
                gu_debug ("gcs_handle_actions returned %d: %s",
                          ret, strerror(-ret));
                break;
            }
            if (gu_likely(ret <= 0)) continue; // not for application
        }

        /* deliver to application (note matching assert in the bottom-half of
         * gcs_repl()) */
        if (gu_likely (act_id >= 0 || act_type != GCS_ACT_TORDERED)) {
            /* successful delivery - increment local order */
            this_act_id = conn->local_act_id++;
        }

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
            gcs_slave_act_t* slave_act = gu_fifo_get_tail (conn->recv_q);

            if (gu_likely (NULL != slave_act)) {

                slave_act->act_size     = act_size;
                slave_act->act_type     = act_type;
                slave_act->act_id       = act_id;
                slave_act->local_act_id = this_act_id;
                slave_act->action       = action;

                ret = gcs_fc_stop(conn); // Send FC_STOP if it is necessary

                gu_fifo_push_tail (conn->recv_q); // release queue

                if (gu_unlikely(ret)) {
                    gu_debug ("gcs_fc_stop() returned %d: %s",
                              ret, strerror(-ret));
                    break;
                }
            }
            else {
                assert (GCS_CONN_CLOSED == conn->state);
                ret = -EBADFD;
                break;
            }

//            gu_info("Received foreign action of type %d, size %d, id=%llu, "
//                    "action %p", act_type, act_size, this_act_id, action);
	}
        // below I can't use any references to act
//       gu_debug("Received action of type %d, size %d, id=%llu, local id=%llu",
//                 act_type, act_size, act_id, conn->local_act_id);
    }

    if (ret > 0) ret = 0;
    gu_info ("RECV thread exiting %d: %s", ret, strerror(-ret));
    return NULL;
}

/* Opens connection to group */
long gcs_open (gcs_conn_t* conn, const char* channel, const char* url)
{
    long ret = 0;

//    if ((ret = gcs_queue_reset (conn->recv_q))) return ret;

    if (!(ret = gu_mutex_lock (&conn->lock))) {

        if (GCS_CONN_CLOSED == conn->state) {

            if (!(ret = gcs_core_open (conn->core, channel, url))) {

                if (0 < (ret = gcs_core_set_pkt_size (conn->core,
                                                      GCS_DEFAULT_PKT_SIZE))) {

                    if (!(ret = gu_thread_create (&conn->recv_thread,
                                                  NULL,
                                                  gcs_recv_thread,
                                                  conn))) {
                        conn->state = GCS_CONN_OPEN; // by default
                        gu_info ("Opened channel '%s'", channel);
                        goto out;
                    }
                    else {
                        gu_error ("Failed to create main receive thread: "
                                  "%ld (%s)", ret, strerror(-ret));
                    }

                }
                else {
                    gu_error ("Failed to set packet size: %ld (%s)",
                              ret, strerror(-ret));        
                }

                gcs_core_close (conn->core);
            }
            else {
                gu_error ("Failed to open channel '%s' at '%s': %d (%s)",
                          channel, url, ret, strerror(-ret));
            }
        }
        else {
            ret = -EBADFD;
        }
out:
        gu_mutex_unlock (&conn->lock);
    }

    return ret;
}

/* Closes group connection */
/* After it returns, application should have all time in the world to cancel
 * and join threads which try to access the handle, before calling gcs_destroy()
 * on it. */
long gcs_close (gcs_conn_t *conn)
{
    long ret;

    if ((ret = gu_mutex_lock (&conn->lock))) return ret;
    {
        gcs_act_t** act_ptr;

        if (GCS_CONN_CLOSED <= conn->state)
        {
            gu_mutex_unlock (&conn->lock);
            return -EBADFD;
        }

        /** @note: We have to close connection before joining RECV thread
         *         since it may be blocked in gcs_backend_recv().
         *         That also means, that RECV thread should surely exit here. */
        if ((ret = gcs_core_close (conn->core))) {
            return ret;
        }

        gu_thread_join (conn->recv_thread, NULL);
        gu_debug ("recv_thread() joined.");

        conn->state = GCS_CONN_CLOSED;
        conn->err   = -ECONNABORTED;

        /* At this point (state == CLOSED) no new threads should be able to
         * queue for repl (check gcs_repl()), and recv thread is joined, so no
         * new actions will be received. Abort threads that are still waiting
         * in repl queue */
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
        gcs_fifo_lite_close (conn->repl_q);
    }
    gu_mutex_unlock (&conn->lock);

    /* wake all gcs_recv() threads () */
    // FIXME: this can block waiting for applicaiton threads to fetch all
    // items. In certain situations this can block forever. Ticket #113
    gu_info ("Closing slave action queue.");
    gu_fifo_close (conn->recv_q);

    return ret;
}

/* Frees resources associated with GCS connection handle */
long gcs_destroy (gcs_conn_t *conn)
{
    long err;

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
    gu_fifo_destroy (conn->recv_q);

    if ((err = gcs_core_destroy (conn->core))) {
        gu_debug ("Error destroying core: %d (%s)", err, strerror(-err));
        return err;
    }

    /* This must not last for long */
    while (gu_mutex_destroy (&conn->lock));
    
    gu_free (conn);

    return 0;
}

/* Puts action in the send queue and returns */
long gcs_send (gcs_conn_t*          conn,
               const void*          action,
               const size_t         act_size,
               const gcs_act_type_t act_type)
{
    long ret = -ENOTCONN;

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
long gcs_repl (gcs_conn_t          *conn,
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
            // if (conn->state >= GCS_CONN_CLOSE) ret will be -ENOTCONN
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
                    assert (ret == (ssize_t)act_size);
                }
            }
            gu_mutex_unlock (&conn->lock);
        }
        assert(ret);
        /* now having unlocked repl_q we can go waiting for action delivery */
//        if (ret >= 0 && GCS_CONN_CLOSED > conn->state) {
        if (ret >= 0) {
            gu_cond_wait (&act.wait_cond, &act.wait_mutex);

            *act_id       = act.act_id;       /* set by recv_thread */
            *local_act_id = act.local_act_id; /* set by recv_thread */

            if (act.act_id < 0) {
                assert (GCS_SEQNO_ILL    == act.local_act_id ||
                        GCS_ACT_TORDERED != act_type);
                if (act.act_id == GCS_SEQNO_ILL) {
                    /* action was not replicated for some reason */
                    ret = -EINTR;
                }
                else {
                    /* core provided an error code */
                    ret = act.act_id;
                }
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

long gcs_request_state_transfer (gcs_conn_t  *conn,
                                 const void  *req,
                                 size_t       size,
                                 const char  *donor,
                                 gcs_seqno_t *local)
{
    gcs_seqno_t global;
    long   ret       = -ENOMEM;
    size_t donor_len = strlen(donor) + 1; // include terminating \0
    size_t rst_size  = size + donor_len;
    void*  rst       = gu_malloc (rst_size);

    if (rst) {
        /* RST format: |donor name|\0|app request|
         * anything more complex will require a special (de)serializer.
         * NOTE: this is sender part. Check gcs_group_handle_state_request()
         *       for the receiver part. */
        memcpy (rst, donor, donor_len);
        memcpy (rst + donor_len, req, size);

        ret = gcs_repl(conn, rst, rst_size, GCS_ACT_STATE_REQ, &global, local);

        if (ret > 0) {
            assert (ret == (ssize_t)rst_size);
            assert (global >= 0);
        }

        ret = global; // index of donor or error code is in the global seqno

        gu_free (rst);
    }

    return ret;
}

/* Returns when an action from another process is received */
long gcs_recv (gcs_conn_t*     conn,
               void**          action,
               size_t*         act_size,
               gcs_act_type_t* act_type,
               gcs_seqno_t*    act_id,
               gcs_seqno_t*    local_act_id)
{
    long             err;
    gcs_slave_act_t *act = NULL;

    *act_size     = 0;
    *act_type     = GCS_ACT_ERROR;
    *act_id       = GCS_SEQNO_ILL;
    *local_act_id = GCS_SEQNO_ILL;
    *action       = NULL;

    if ((act = gu_fifo_get_head (conn->recv_q)))
    {
        // FIXME: We have successfully received an action, but failed to send
        // important control message. What do we do? Inability to send CONT can
        // block the whole cluster. There are only conn->lower_limit attempts
        // to do that. Perhaps if the last attempt fails, we should crash.
        if (GCS_CONN_SYNCED == conn->state && (err = gcs_fc_cont(conn))) {
            if (conn->queue_len > 0) {
                gu_warn ("Failed to send CONT message: %d (%s). "
                         "Attempts left: %ld",
                         err, strerror(-err), conn->queue_len);
            }
            else {
                gu_fatal ("Last opportunity to send CONT message failed: "
                          "%d (%s). Crashing to avoid cluster lock-up",
                          err, strerror(-err));
                abort();
            }
        }
        else if (GCS_CONN_JOINED == conn->state && (err=gcs_send_sync(conn))) {
            gu_warn ("Failed to send SYNC message: %d (%s). Will try later.",
                     err, strerror(-err));
        }

        *act_size     = act->act_size;
        *act_type     = act->act_type;
        *act_id       = act->act_id;
        *local_act_id = act->local_act_id;
        *action       = (uint8_t*)act->action;

        gu_fifo_pop_head (conn->recv_q); // release the queue

        return *act_size;
    }
    else {
        assert (GCS_CONN_CLOSED == conn->state);
        return GCS_CLOSED_ERROR;
    }
}

long
gcs_wait (gcs_conn_t* conn)
{
    if (gu_likely(GCS_CONN_SYNCED == conn->state)) {
       return (conn->stop_count > 0 || (conn->queue_len > conn->upper_limit));
    }
    else {
        switch (conn->state) {
        case GCS_CONN_OPEN:
            return -ENOTCONN;
        case GCS_CONN_CLOSED:
        case GCS_CONN_DESTROYED:
            return GCS_CLOSED_ERROR;
        default:
            return 1; // wait until get sync
        }
    }
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

long
gcs_join (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    return gcs_core_send_join (conn->core, seqno);
}
