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
#include "gcs_seqno.h"
#include "gcs_core.h"
#include "gcs_fifo_lite.h"

const char* gcs_act_type_to_str (gcs_act_type_t type)
{
    static const char* str[GCS_ACT_UNKNOWN + 1] =
    {
        "TORDERED", "COMMIT_CUT", "STATE_REQUEST", "CONFIGURATION",
        "JOIN", "SYNC", "FLOW", "SERVICE", "ERROR", "UNKNOWN"
    };

    if (((unsigned long)type) < GCS_ACT_UNKNOWN) return str[type];

    return str[GCS_ACT_UNKNOWN];
}

static const long GCS_MAX_REPL_THREADS = 16384;

// Flow control parameters
static const long   fc_base_queue_limit = 16;
static const double fc_resume_factor    = 0.5;

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

static const char* gcs_conn_state_str[GCS_CONN_DESTROYED + 1] =
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
    gu_mutex_t   fc_lock;
    uint32_t     conf_id;     // configuration ID
    long         stop_sent;   // how many STOPs - CONTs were send
    long         stop_count;  // counts stop requests received
    long         queue_len;   // slave queue length
    long         upper_limit; // upper slave queue limit
    long         lower_limit; // lower slave queue limit

    /* gcs_core object */
    gcs_core_t  *core; // the context that is returned by
                       // the core group communication system
};

// Oh C++, where art thou?
struct gcs_recv_act
{
    struct gcs_act_rcvd rcvd;
    gcs_seqno_t         local_id;
};

struct gcs_repl_act
{
    gcs_seqno_t    act_id;
    gcs_seqno_t    local_act_id;
    const void*    action;
    ssize_t        act_size;
    gcs_act_type_t act_type;
    gu_mutex_t     wait_mutex;
    gu_cond_t      wait_cond;
};

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
                                                 sizeof (struct gcs_repl_act*));

            if (conn->repl_q) {
                size_t recv_q_len = GU_AVPHYS_PAGES * GU_PAGE_SIZE /
                                    sizeof(struct gcs_recv_act) / 4;
                gu_debug ("Requesting recv queue len: %zu", recv_q_len);
                conn->recv_q = gu_fifo_create (recv_q_len,
                                               sizeof(struct gcs_recv_act));

                if (conn->recv_q) {
                    conn->state        = GCS_CONN_CLOSED;
                    conn->my_idx       = -1;
                    conn->local_act_id = GCS_SEQNO_FIRST;
                    gu_mutex_init (&conn->lock, NULL);
                    gu_mutex_init (&conn->fc_lock, NULL);
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

/*!
 * Checks if we should freak out on send/recv errors.
 * Sometimes errors are ok, e.g. when attempting to send FC_CONT message
 * on a closing connection. This can happen because GCS connection state
 * change propagation from lower layers to upper layers is not atomic.
 *
 * @param err     error code returned by send/recv function
 * @param warning warning to log if necessary
 * @return        0 if error can be ignored, original err value if not
 */
static long
gcs_check_error (long err, const char* warning)
{
    switch (err)
    {
    case -ENOTCONN:
    case -ECONNABORTED:
        if (NULL != warning) {
            gu_warn ("%s: %d (%s)", warning, err, strerror(-err));
        }
        err = 0;
        break;
    default:;
    }

    return err;
}

/* To be called under slave queue lock. Returns true if FC_STOP must be sent */
static inline bool
gcs_fc_stop_begin (gcs_conn_t* conn)
{
    long err = 0;

    bool ret = (conn->queue_len  >  conn->upper_limit &&
                GCS_CONN_SYNCED  == conn->state       &&
                conn->stop_count <= 0                 &&
                conn->stop_sent  <= 0                 &&
                !(err = gu_mutex_lock (&conn->fc_lock)));

    if (gu_unlikely(err)) {
            gu_fatal ("Mutex lock failed: %d (%s)", err, strerror(err));
            abort();
    }

    conn->stop_sent += ret;

    return ret;
}

/* Complement to gcs_fc_stop_begin. */
static inline long
gcs_fc_stop_end (gcs_conn_t* conn)
{
    long ret;
    struct gcs_fc fc  = { htogl(conn->conf_id), 1 };

    gu_debug ("SENDING FC_STOP (%llu)", conn->local_act_id); //track freq

    ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));

    if (ret >= 0) {
        ret = 0;
    }
    else {
        conn->stop_sent--;
        assert (conn->stop_sent >= 0);
    }

    gu_mutex_unlock (&conn->fc_lock);

    ret = gcs_check_error (ret, "Failed to send FC_STOP signal");

    return ret;
}

/* To be called under slave queue lock. Returns true if FC_CONT must be sent */
static inline bool
gcs_fc_cont_begin (gcs_conn_t* conn)
{
    long err = 0;

    bool ret = (conn->lower_limit >= conn->queue_len &&
                conn->stop_sent   >  0               &&
                GCS_CONN_SYNCED   == conn->state     &&
                !(err = gu_mutex_lock (&conn->fc_lock)));

    if (gu_unlikely(err)) {
        gu_fatal ("Mutex lock failed: %d (%s)", err, strerror(err));
        abort();
    }

    conn->stop_sent -= ret; // decrement optimistically to allow for parallel
                            // recv threads
    return ret;
}

/* Complement to gcs_fc_cont_begin() */
static inline long
gcs_fc_cont_end (gcs_conn_t* conn)
{
    long ret;
    struct gcs_fc fc  = { htogl(conn->conf_id), 0 };

    assert (GCS_CONN_SYNCED == conn->state);

    gu_debug ("SENDING FC_CONT (%llu)", conn->local_act_id);

    ret = gcs_core_send_fc (conn->core, &fc, sizeof(fc));

    if (gu_likely (ret >= 0)) {
        ret = 0;
    }

    conn->stop_sent += (ret != 0); // fix count in case of error

    gu_mutex_unlock (&conn->fc_lock);

    ret = gcs_check_error (ret, "Failed to send FC_CONT signal");

    return ret;
}

static inline long
gcs_send_sync (gcs_conn_t* conn) {
    long ret = 0;

    if (conn->lower_limit >= conn->queue_len &&
        GCS_CONN_JOINED   == conn->state) {
        // tripped lower slave queue limit, send SYNC message
        gu_debug ("SENDING SYNC");
        ret = gcs_core_send_sync (conn->core, 0);
        if (ret >= 0) {
            ret = 0;
        }
        ret = gcs_check_error (ret, "Failed to send SYNC signal");
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

    gu_warn ("Rejecting SST request in state '%s'. Joiner should be restarted.",
             gcs_conn_state_str[conn->state]);

    if (conn->state < GCS_CONN_OPEN){
        ssize_t err;
        gu_warn ("Received State Transfer Request in wrong state %s. "
                 "Rejecting.", gcs_conn_state_str[conn->state]);
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
gcs_become_joined (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    /* See also gcs_handle_act_conf () for a case of cluster bootstrapping */
    if (GCS_CONN_JOINER == conn->state ||
        GCS_CONN_DONOR  == conn->state) {
        long ret;

        gu_info ("Shifting %s -> %s at %lld", gcs_conn_state_str[conn->state],
                 gcs_conn_state_str[GCS_CONN_JOINED], seqno);

        conn->state = GCS_CONN_JOINED;

        /* One of the cases when the node can become SYNCED */
        if ((ret = gcs_send_sync (conn))) {
            gu_warn ("Sending SYNC failed: %ld (%s)", ret, strerror (-ret));
        }
    }
//    else if (conn->state < GCS_CONN_OPEN){
    else {
        gu_warn ("Received JOIN action in wrong state %s",
                 gcs_conn_state_str[conn->state]);
        assert (0);
    }
}

static void
gcs_become_synced (gcs_conn_t* conn)
{
    if (GCS_CONN_JOINED == conn->state) {
        conn->state = GCS_CONN_SYNCED;
    }
    else if (conn->state <= GCS_CONN_OPEN && conn->state > GCS_CONN_SYNCED) {
        gu_warn ("Received SYNC action in wrong state %s",
                 gcs_conn_state_str[conn->state]);
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

    /* reset flow control as membership is most likely changed */
    gu_fifo_lock(conn->recv_q);
    {
        if (gu_mutex_lock (&conn->fc_lock)) {
            gu_fatal ("Failed to lock mutex.");
            abort();
        }

        conn->conf_id     = conf->conf_id;
        conn->stop_sent   = 0;
        conn->stop_count  = 0;
        conn->upper_limit = fc_base_queue_limit * sqrt(conf->memb_num - 1) + .5;
        conn->lower_limit = conn->upper_limit * fc_resume_factor + .5;

        gu_mutex_unlock (&conn->fc_lock);
    }
    gu_fifo_release (conn->recv_q);

    gu_info ("Flow-control interval: [%ld, %ld]",
             conn->lower_limit, conn->upper_limit);

    if (conf->conf_id < 0) {
        if (0 == conf->memb_num) {
            assert (conf->my_idx < 0);
            gu_info ("Received SELF-LEAVE. Closing connection.");
            conn->state = GCS_CONN_CLOSED;
        }
        else {
            gu_info ("Received NON-PRIMARY.");
            gcs_become_open (conn);
        }
        return;
    }

    if (conf->memb_num < 1) {
        gu_fatal ("Internal error: PRIM configuration with %d nodes",
                  conf->memb_num);
        abort();
    }

    if (conf->my_idx < 0 || conf->my_idx >= conf->memb_num) {
        gu_fatal ("Internal error: index of this node (%d) is out of bounds: "
                  "[%d, %d]", conf->my_idx, 0, conf->memb_num - 1);
        abort();
    }

    if (conf->st_required) {
        gcs_become_open (conn);
    }
    else if (1             == conf->conf_id  &&
             1             == conf->memb_num &&
             GCS_CONN_OPEN == conn->state) {
        gu_info ("Initializing JOINED state as the first member of the group.");
        conn->state = GCS_CONN_JOINED;
    }

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
gcs_handle_actions (gcs_conn_t* conn,
                    const struct gcs_act_rcvd* rcvd)
{
    long ret = 0;

    switch (rcvd->act.type) {
    case GCS_ACT_FLOW:
    { // this is frequent, so leave it inlined.
        const struct gcs_fc* fc = rcvd->act.buf;

        assert (sizeof(*fc) == rcvd->act.buf_len);

        if (gtohl(fc->conf_id) != (uint32_t)conn->conf_id) {
            // obsolete fc request
            break;
        }
//        gu_info ("RECEIVED %s", fc->stop ? "STOP" : "CONT");
        conn->stop_count += ((fc->stop != 0) << 1) - 1; // +1 if !0, -1 if 0
        break;
    }
    case GCS_ACT_CONF:
        gcs_handle_act_conf (conn, rcvd->act.buf);
        ret = 1;
        break;
    case GCS_ACT_STATE_REQ:
        if ((gcs_seqno_t)conn->my_idx == rcvd->id) {
            gu_info ("Got GCS_ACT_STATE_REQ to %lld, my idx: %ld",
                     rcvd->id, conn->my_idx);
            ret = gcs_become_donor (conn);
            gu_info ("Becoming donor: %s", 1 == ret ? "yes" : "no");
        }
        else {
            if (rcvd->id >= 0) gcs_become_joiner (conn);
            ret = 1; // pass to gcs_request_state_transfer() caller.
        }
        break; 
    case GCS_ACT_JOIN:
        gu_debug ("Got GCS_ACT_JOIN");
        gcs_become_joined (conn, gcs_seqno_le(*(gcs_seqno_t*)rcvd->act.buf));
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
    ssize_t          ret  = -ECONNABORTED;
    gcs_conn_t*      conn = arg;

    // To avoid race between gcs_open() and the following state check in while()
    gu_mutex_lock   (&conn->lock);
    gu_mutex_unlock (&conn->lock);

    while (conn->state < GCS_CONN_CLOSED)
    {
        gcs_seqno_t this_act_id = GCS_SEQNO_ILL;
        struct gcs_repl_act** repl_act_ptr;
        struct gcs_act_rcvd   rcvd;
        bool                  act_is_local;

        ret = gcs_core_recv (conn->core, &rcvd, &act_is_local);

        if (gu_unlikely(ret <= 0)) {
            struct gcs_recv_act *err_act = gu_fifo_get_tail(conn->recv_q);

            assert (NULL          == rcvd.act.buf);
            assert (0             == rcvd.act.buf_len);
            assert (GCS_ACT_ERROR == rcvd.act.type);
            assert (GCS_SEQNO_ILL == rcvd.id);
            assert (!act_is_local);

            err_act->rcvd     = rcvd;
            err_act->local_id = GCS_SEQNO_ILL;

            gu_fifo_push_tail(conn->recv_q);

            gu_debug ("gcs_core_recv returned %d: %s", ret, strerror(-ret));
            break;
        }

//        gu_info ("Received action type: %d, size: %d, global seqno: %lld",
//                 act_type, act_size, (long long)act_id);

        assert (rcvd.act.type < GCS_ACT_ERROR);
        assert (ret == rcvd.act.buf_len);

        if (gu_unlikely(rcvd.act.type >= GCS_ACT_STATE_REQ)) {
            ret = gcs_handle_actions (conn, &rcvd);

            if (gu_unlikely(ret < 0)) {         // error
                gu_debug ("gcs_handle_actions returned %d: %s",
                          ret, strerror(-ret));
                break;
            }

            if (gu_likely(ret <= 0)) continue; // not for application
        }

        /* deliver to application (note matching assert in the bottom-half of
         * gcs_repl()) */
        if (gu_likely (rcvd.id >= 0 || rcvd.act.type != GCS_ACT_TORDERED)) {
            /* successful delivery - increment local order */
            this_act_id = conn->local_act_id++;
        }

        if (act_is_local                                           &&
            (repl_act_ptr = gcs_fifo_lite_get_head (conn->repl_q)) &&
            (gu_likely ((*repl_act_ptr)->action == rcvd.act.buf)   ||
             /* at this point repl_q is locked and we need to unlock it and
              * return false to fall to the 'else' branch; unlikely case */ 
             (gcs_fifo_lite_release (conn->repl_q), false)))
        {
            /* local action from repl_q */
            struct gcs_repl_act* repl_act = *repl_act_ptr;
            gcs_fifo_lite_pop_head (conn->repl_q);

            assert (repl_act->action   == rcvd.act.buf);
            assert (repl_act->act_size == rcvd.act.buf_len);

            repl_act->act_id       = rcvd.id;
            repl_act->local_act_id = this_act_id;

            gu_mutex_lock   (&repl_act->wait_mutex);
            gu_cond_signal  (&repl_act->wait_cond);
            gu_mutex_unlock (&repl_act->wait_mutex);
        }
        else if (gu_likely(this_act_id >= 0))
        {
            /* remote/non-replicated action */
            struct gcs_recv_act* recv_act = gu_fifo_get_tail (conn->recv_q);

            if (gu_likely (NULL != recv_act)) {

                recv_act->rcvd     = rcvd;
                recv_act->local_id = this_act_id;
  
                conn->queue_len = gu_fifo_length (conn->recv_q) + 1;
                bool send_stop  = gcs_fc_stop_begin (conn);

                gu_fifo_push_tail (conn->recv_q); // release queue

                if (gu_unlikely(send_stop) && (ret = gcs_fc_stop_end(conn))) {
                    gu_error ("gcs_fc_stop() returned %d: %s",
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
//                    "action %p", rcvd.act.type, rcvd.act.buf_len,
//                    this_act_id, rcvd.act.buf);
        }
        else if (conn->my_idx == rcvd.sender_idx)
        {
#ifndef NDEBUG
            gu_warn ("Protocol violation: unordered local action not in repl_q:"
                     " { {%p, %zd, %s}, %ld, %lld }, ignoring.",
                     rcvd.act.buf, rcvd.act.buf_len,
                     gcs_act_type_to_str(rcvd.act.type), rcvd.sender_idx,
                     rcvd.id);
#endif
            assert(0);
        }
        else
        {
            gu_fatal ("Protocol violation: unordered remote action: "
                      "{ {%p, %zd, %s}, %ld, %lld }",
                      rcvd.act.buf, rcvd.act.buf_len,
                      gcs_act_type_to_str(rcvd.act.type), rcvd.sender_idx,
                      rcvd.id);
            assert (0);
            ret = -ENOTRECOVERABLE;
            break;
        }
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
        struct gcs_repl_act** act_ptr;

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
        gu_info ("recv_thread() joined.");

        conn->state = GCS_CONN_CLOSED;
        conn->err   = -ECONNABORTED;

        /* At this point (state == CLOSED) no new threads should be able to
         * queue for repl (check gcs_repl()), and recv thread is joined, so no
         * new actions will be received. Abort threads that are still waiting
         * in repl queue */
        while ((act_ptr = gcs_fifo_lite_get_head (conn->repl_q))) {
            struct gcs_repl_act* act = *act_ptr;
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
    while (gu_mutex_destroy (&conn->fc_lock));

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
    struct gcs_repl_act act = { .act_size     = act_size,
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
            struct gcs_repl_act** act_ptr;
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
                    /* remove item from the queue, it will never be delivered */
                    gu_warn ("Send action {%p, %zd, %s} returned %d (%s)",
                             action, act_size, gcs_act_type_to_str(act_type),
                             ret, strerror(-ret));
                    if (!gcs_fifo_lite_remove (conn->repl_q)) {
                        gu_fatal ("Failed to remove unsent item from repl_q");
                        assert(0);
                        ret = -ENOTRECOVERABLE;
                    }
                }
                else {
                    assert (ret == (ssize_t)act_size);
                }
            }
            gu_mutex_unlock (&conn->lock);

            assert(ret);
            /* now we can go waiting for action delivery */
//        if (ret >= 0 && GCS_CONN_CLOSED > conn->state) {
            if (ret >= 0) {
                gu_cond_wait (&act.wait_cond, &act.wait_mutex);

                if (act.act_id < 0) {
                    assert (GCS_SEQNO_ILL    == act.local_act_id ||
                            GCS_ACT_TORDERED != act_type);
                    if (act.act_id == GCS_SEQNO_ILL) {
                        /* action was not replicated for some reason */
                        ret = -EINTR;
                    }
                    else {
                        /* core provided an error code in act_id */
                        ret = act.act_id;
                    }

                    act.act_id = GCS_SEQNO_ILL;
                }

                *act_id       = act.act_id;       /* set by recv_thread */
                *local_act_id = act.local_act_id; /* set by recv_thread */
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
            ret = global; // index of donor or error code is in the global seqno
        }

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
    long                 err;
    struct gcs_recv_act *act = NULL;

    *act_size     = 0;
    *act_type     = GCS_ACT_ERROR;
    *act_id       = GCS_SEQNO_ILL;
    *local_act_id = GCS_SEQNO_ILL;
    *action       = NULL;

    if ((act = gu_fifo_get_head (conn->recv_q)))
    {
        *action       = (void*)act->rcvd.act.buf;
        *act_size     = act->rcvd.act.buf_len;
        *act_type     = act->rcvd.act.type;
        *act_id       = act->rcvd.id;
        *local_act_id = act->local_id;

        conn->queue_len = gu_fifo_length (conn->recv_q) - 1;
        bool send_cont  = gcs_fc_cont_begin(conn);

        gu_fifo_pop_head (conn->recv_q); // release the queue

        if (gu_unlikely(send_cont) && (err = gcs_fc_cont_end(conn))) {
            // We have successfully received an action, but failed to send
            // important control message. What do we do? Inability to send CONT
            // can block the whole cluster. There are only conn->queue_len - 1
            // attempts to do that (that's how many times we'll get here).
            // Perhaps if the last attempt fails, we should crash.
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
        case GCS_CONN_JOINER:
            return -ECONNABORTED;
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
