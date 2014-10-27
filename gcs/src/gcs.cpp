/*
 * Copyright (C) 2008-2014 Codership Oy <info@codership.com>
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

#include "gcs_priv.hpp"
#include "gcs_params.hpp"
#include "gcs_fc.hpp"
#include "gcs_seqno.hpp"
#include "gcs_core.hpp"
#include "gcs_fifo_lite.hpp"
#include "gcs_sm.hpp"
#include "gcs_gcache.hpp"

const char* gcs_node_state_to_str (gcs_node_state_t state)
{
    static const char* str[GCS_NODE_STATE_MAX + 1] =
    {
        "NON-PRIMARY",
        "PRIMARY",
        "JOINER",
        "DONOR",
        "JOINED",
        "SYNCED",
        "UNKNOWN"
    };

    if (state < GCS_NODE_STATE_MAX) return str[state];

    return str[GCS_NODE_STATE_MAX];
}

const char* gcs_act_type_to_str (gcs_act_type_t type)
{
    static const char* str[GCS_ACT_UNKNOWN + 1] =
    {
        "TORDERED", "COMMIT_CUT", "STATE_REQUEST", "CONFIGURATION",
        "JOIN", "SYNC", "FLOW", "SERVICE", "ERROR", "UNKNOWN"
    };

    if (type < GCS_ACT_UNKNOWN) return str[type];

    return str[GCS_ACT_UNKNOWN];
}

static const long GCS_MAX_REPL_THREADS = 16384;

typedef enum
{
    GCS_CONN_SYNCED,   // caught up with the rest of the group
    GCS_CONN_JOINED,   // state transfer complete
    GCS_CONN_DONOR,    // in state transfer, donor
    GCS_CONN_JOINER,   // in state transfer, joiner
    GCS_CONN_PRIMARY,  // in primary conf, needs state transfer
    GCS_CONN_OPEN,     // just connected to group, non-primary
    GCS_CONN_CLOSED,
    GCS_CONN_DESTROYED,
    GCS_CONN_ERROR,
    GCS_CONN_STATE_MAX
}
gcs_conn_state_t;

#define GCS_CLOSED_ERROR -EBADFD; // file descriptor in bad state

static const char* gcs_conn_state_str[GCS_CONN_STATE_MAX] =
{
    "SYNCED",
    "JOINED",
    "DONOR/DESYNCED",
    "JOINER",
    "PRIMARY",
    "OPEN",
    "CLOSED",
    "DESTROYED",
    "ERROR"
};

static bool const GCS_FC_STOP = true;
static bool const GCS_FC_CONT = false;

/** Flow control message */
struct gcs_fc_event
{
    uint32_t conf_id; // least significant part of configuraiton seqno
    uint32_t stop;    // boolean value
}
__attribute__((__packed__));

struct gcs_conn
{
    long  my_idx;
    long  memb_num;
    char* my_name;
    char* channel;
    char* socket;

    gcs_conn_state_t  state;

    gu_config_t*      config;
    bool              config_is_local;
    struct gcs_params params;

    gcache_t*    gcache;

    gcs_sm_t*    sm;

    gcs_seqno_t  local_act_id; /* local seqno of the action */
    gcs_seqno_t  global_seqno;

    /* A queue for threads waiting for replicated actions */
    gcs_fifo_lite_t* repl_q;
    gu_thread_t      send_thread;

    /* A queue for threads waiting for received actions */
    gu_fifo_t*   recv_q;
    ssize_t      recv_q_size;
    gu_thread_t  recv_thread;

    /* Message receiving timeout - absolute date in nanoseconds */
    long long    timeout;

    /* Flow Control */
    gu_mutex_t   fc_lock;
    uint32_t     conf_id;             // configuration ID
    long         stop_sent;           // how many STOPs - CONTs were sent
    long         stop_count;          // counts stop requests received
    long         queue_len;           // slave queue length
    long         upper_limit;         // upper slave queue limit
    long         lower_limit;         // lower slave queue limit
    long         fc_offset;           // offset for catchup phase
    gcs_conn_state_t max_fc_state;    // maximum state when FC is enabled
    long         stats_fc_sent;       // FC stats counters
    long         stats_fc_received;   //
    gcs_fc_t     stfc; // state transfer FC object

    /* #603, #606 join control */
    bool        volatile need_to_join;
    gcs_seqno_t volatile join_seqno;

    /* sync control */
    bool         sync_sent;

    /* gcs_core object */
    gcs_core_t*  core; // the context that is returned by
                       // the core group communication system

    int inner_close_count; // how many times _close has been called.
    int outer_close_count; // how many times gcs_close has been called.
};

// Oh C++, where art thou?
struct gcs_recv_act
{
    struct gcs_act_rcvd rcvd;
    gcs_seqno_t         local_id;
};

struct gcs_repl_act
{
    const struct gu_buf* act_in;
    struct gcs_action*   action;
    gu_mutex_t           wait_mutex;
    gu_cond_t            wait_cond;
    gcs_repl_act(const struct gu_buf* a_act_in, struct gcs_action* a_action)
      :
        act_in(a_act_in),
        action(a_action)
    { }
};

/*! Releases resources associated with parameters */
static void
_cleanup_params (gcs_conn_t* conn)
{
    if (conn->config_is_local) gu_config_destroy(conn->config);
}

/*! Creates local configuration object if no external is submitted */
static long
_init_params (gcs_conn_t* conn, gu_config_t* conf)
{
    long rc;

    conn->config = conf;
    conn->config_is_local = false;

    if (!conn->config) {
        conn->config = gu_config_create();

        if (conn->config) {
            conn->config_is_local = true;
        }
        else {
            rc = -ENOMEM;
            goto enomem;
        }
    }

    rc = gcs_params_init (&conn->params, conn->config);

    if (!rc) return 0;

    _cleanup_params (conn);

enomem:

    gu_error ("Parameter initialization failed: %s", strerror (-rc));

    return rc;
}

/* Creates a group connection handle */
gcs_conn_t*
gcs_create (gu_config_t* const conf, gcache_t* const gcache,
            const char* const node_name, const char* const inc_addr,
            int const repl_proto_ver, int const appl_proto_ver)
{
    gcs_conn_t* conn = GU_CALLOC (1, gcs_conn_t);

    if (!conn) {
        gu_error ("Could not allocate GCS connection handle: %s",
                  strerror (ENOMEM));
        return NULL;
    }

    if (_init_params (conn, conf)) {
        goto init_params_failed;
    }

    if (gcs_fc_init (&conn->stfc,
                     conn->params.recv_q_hard_limit,
                     conn->params.recv_q_soft_limit,
                     conn->params.max_throttle)) {
        gu_error ("FC initialization failed");
        goto fc_init_failed;
    }

    conn->state = GCS_CONN_DESTROYED;
    conn->core  = gcs_core_create (conf, gcache, node_name, inc_addr,
                                   repl_proto_ver, appl_proto_ver);
    if (!conn->core) {
        gu_error ("Failed to create core.");
        goto core_create_failed;
    }

    conn->repl_q = gcs_fifo_lite_create (GCS_MAX_REPL_THREADS,
                                         sizeof (struct gcs_repl_act*));
    if (!conn->repl_q) {
        gu_error ("Failed to create repl_q.");
        goto repl_q_failed;
    }

    {
        size_t recv_q_len = gu_avphys_bytes() / sizeof(struct gcs_recv_act) / 4;

        gu_debug ("Requesting recv queue len: %zu", recv_q_len);
        conn->recv_q = gu_fifo_create (recv_q_len, sizeof(struct gcs_recv_act));
    }
    if (!conn->recv_q) {
        gu_error ("Failed to create recv_q.");
        goto recv_q_failed;
    }

    conn->sm = gcs_sm_create(1<<16, 1);

    if (!conn->sm) {
        gu_error ("Failed to create send monitor");
        goto sm_create_failed;
    }

    conn->state        = GCS_CONN_CLOSED;
    conn->my_idx       = -1;
    conn->local_act_id = GCS_SEQNO_FIRST;
    conn->global_seqno = 0;
    conn->fc_offset    = 0;
    conn->timeout      = GU_TIME_ETERNITY;
    conn->gcache       = gcache;
    conn->max_fc_state = conn->params.sync_donor ?
        GCS_CONN_DONOR : GCS_CONN_JOINED;

    gu_mutex_init (&conn->fc_lock, NULL);

    return conn; // success

sm_create_failed:

    gu_fifo_destroy (conn->recv_q);

recv_q_failed:

    gcs_fifo_lite_destroy (conn->repl_q);

repl_q_failed:

    gcs_core_destroy (conn->core);

core_create_failed:
fc_init_failed:

    _cleanup_params (conn);

init_params_failed:

    gu_free (conn);

    gu_error ("Failed to create GCS connection handle.");
    return NULL; // failure
}

long
gcs_init (gcs_conn_t* conn, gcs_seqno_t seqno, const uint8_t uuid[GU_UUID_LEN])
{
    if (GCS_CONN_CLOSED == conn->state) {
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

static inline long
gcs_send_fc_event (gcs_conn_t* conn, bool stop)
{
    struct gcs_fc_event fc  = { htogl(conn->conf_id), stop };
    return gcs_core_send_fc (conn->core, &fc, sizeof(fc));
}

/* To be called under slave queue lock. Returns true if FC_STOP must be sent */
static inline bool
gcs_fc_stop_begin (gcs_conn_t* conn)
{
    long err = 0;

    bool ret = (conn->stop_count <= 0                                     &&
                conn->stop_sent  <= 0                                     &&
                conn->queue_len  >  (conn->upper_limit + conn->fc_offset) &&
                conn->state      <= conn->max_fc_state                    &&
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

    gu_debug ("SENDING FC_STOP (local seqno: %lld, fc_offset: %ld)",
              conn->local_act_id, conn->fc_offset);

    ret = gcs_send_fc_event (conn, GCS_FC_STOP);

    if (ret >= 0) {
        ret = 0;
        conn->stats_fc_sent++;
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

    bool queue_decreased = (conn->fc_offset > conn->queue_len &&
                            (conn->fc_offset = conn->queue_len, true));

    bool ret = (conn->stop_sent    >  0                                   &&
                (conn->lower_limit >= conn->queue_len || queue_decreased) &&
                conn->state        <= conn->max_fc_state                  &&
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

    assert (GCS_CONN_DONOR >= conn->state);

    gu_debug ("SENDING FC_CONT (local seqno: %lld, fc_offset: %ld)",
              conn->local_act_id, conn->fc_offset);

    ret = gcs_send_fc_event (conn, GCS_FC_CONT);

    if (gu_likely (ret >= 0)) { ret = 0; }

    conn->stop_sent += (ret != 0); // fix count in case of error

    gu_mutex_unlock (&conn->fc_lock);

    ret = gcs_check_error (ret, "Failed to send FC_CONT signal");

    return ret;
}

/* To be called under slave queue lock. Returns true if SYNC must be sent */
static inline bool
gcs_send_sync_begin (gcs_conn_t* conn)
{
    if (gu_unlikely(GCS_CONN_JOINED == conn->state)) {
        if (conn->lower_limit >= conn->queue_len && !conn->sync_sent) {
            // tripped lower slave queue limit, send SYNC message
            conn->sync_sent = true;
            return true;
        }
#if 0
        else {
            gu_info ("Not sending SYNC: state = %s, queue_len = %ld, "
                     "lower_limit = %ld, sync_sent = %s",
                     gcs_conn_state_str[conn->state], conn->queue_len,
                     conn->lower_limit, conn->sync_sent ? "true" : "false");
        }
#endif
    }

    return false;
}

static inline long
gcs_send_sync_end (gcs_conn_t* conn)
{
    long ret = 0;

    gu_debug ("SENDING SYNC");

    ret = gcs_core_send_sync (conn->core, 0);

    if (gu_likely (ret >= 0)) {
        ret = 0;
    }
    else {
        conn->sync_sent = false;
    }

    ret = gcs_check_error (ret, "Failed to send SYNC signal");

    return ret;
}

static inline long
gcs_send_sync (gcs_conn_t* conn)
{
    if (gcs_send_sync_begin (conn)) {
        return gcs_send_sync_end (conn);
    }
    else {
        return 0;
    }
}

/*!
 * State transition functions - just in case we want to add something there.
 * @todo: need to be reworked, see #231
 */

static bool
gcs_shift_state (gcs_conn_t*      conn,
                 gcs_conn_state_t new_state)
{
    static const bool allowed [GCS_CONN_STATE_MAX][GCS_CONN_STATE_MAX] = {
       // SYNCED JOINED DONOR  JOINER PRIM   OPEN   CLOSED DESTR
        { false, true,  false, false, false, false, false, false }, // SYNCED
        { false, false, true,  true,  false, false, false, false }, // JOINED
        { true,  true,  false, false, false, false, false, false }, // DONOR
        { false, false, false, false, true,  false, false, false }, // JOINER
        { true,  true,  true,  true,  true,  true,  false, false }, // PRIMARY
        { true,  true,  true,  true,  true,  false, true,  false }, // OPEN
        { true,  true,  true,  true,  true,  true,  false, false }, // CLOSED
        { false, false, false, false, false, false, true,  false }  // DESTROYED
    };

    gcs_conn_state_t old_state = conn->state;

    if (!allowed[new_state][old_state]) {
        if (old_state != new_state) {
            gu_warn ("Shifting %s -> %s is not allowed (TO: %lld)",
                     gcs_conn_state_str[old_state],
                     gcs_conn_state_str[new_state], conn->global_seqno);
        }
        return false;
    }

    gu_info ("Shifting %s -> %s (TO: %lld)", gcs_conn_state_str[old_state],
             gcs_conn_state_str[new_state], conn->global_seqno);

    conn->state = new_state;

    return true;
}

static void
gcs_become_open (gcs_conn_t* conn)
{
    gcs_shift_state (conn, GCS_CONN_OPEN);
}

static long
gcs_set_pkt_size (gcs_conn_t *conn, long pkt_size)
{
    if (conn->state != GCS_CONN_CLOSED) return -EPERM; // #600 workaround

    long ret = gcs_core_set_pkt_size (conn->core, pkt_size);

    if (ret >= 0) {
        conn->params.max_packet_size = ret;
        gu_config_set_int64 (conn->config, GCS_PARAMS_MAX_PKT_SIZE,
                             conn->params.max_packet_size);
    }

    return ret;
}

static long
_release_flow_control (gcs_conn_t* conn)
{
    int err = 0;

    if (gu_unlikely(err = gu_mutex_lock (&conn->fc_lock))) {
        gu_fatal ("Mutex lock failed: %d (%s)", err, strerror(err));
        abort();
    }

    if (conn->stop_sent) {
        assert (1 == conn->stop_sent);
        conn->stop_sent--;
        err = gcs_fc_cont_end (conn);
    }
    else {
        gu_mutex_unlock (&conn->fc_lock);
    }

    return err;
}

static void
gcs_become_primary (gcs_conn_t* conn)
{
    if (!gcs_shift_state (conn, GCS_CONN_PRIMARY)) {
        gu_fatal ("Protocol violation, can't continue");
        gcs_close (conn);
        abort();
    }

    long ret;

    if ((ret = _release_flow_control (conn))) {
        gu_fatal ("Failed to release flow control: %ld (%s)",
                  ret, strerror(ret));
        gcs_close (conn);
        abort();
    }
}

static void
gcs_become_joiner (gcs_conn_t* conn)
{
    if (!gcs_shift_state (conn, GCS_CONN_JOINER))
    {
        gu_fatal ("Protocol violation, can't continue");
        assert (0);
        abort();
    }

    if (gcs_fc_init (&conn->stfc,
                     conn->params.recv_q_hard_limit,
                     conn->params.recv_q_soft_limit,
                     conn->params.max_throttle)) {
        gu_fatal ("Becoming JOINER: FC initialization failed, can't continue.");
        abort();
    }

    gcs_fc_reset (&conn->stfc, conn->recv_q_size);
    gcs_fc_debug (&conn->stfc, conn->params.fc_debug);
}

// returns 1 if accepts, 0 if rejects, negative error code if fails.
static long
gcs_become_donor (gcs_conn_t* conn)
{
    if (gcs_shift_state (conn, GCS_CONN_DONOR)) {
        long err = 0;
        if (conn->max_fc_state < GCS_CONN_DONOR) {
            err = _release_flow_control (conn);
        }
        return (0 == err ? 1 : err);
    }

    gu_warn ("Rejecting State Transfer Request in state '%s'. "
             "Joiner should be restarted.", gcs_conn_state_str[conn->state]);

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

static long
_release_sst_flow_control (gcs_conn_t* conn)
{
    long ret = 0;

    do {
        if (conn->stop_sent > 0) {
            ret = gcs_send_fc_event (conn, GCS_FC_CONT);
            conn->stop_sent -= (ret >= 0);
        }
    }
    while (ret < 0 && -EAGAIN == ret); // we need to send CONT here at all costs

    ret = gcs_check_error (ret, "Failed to release SST flow control.");

    return ret;
}

static void
gcs_become_joined (gcs_conn_t* conn)
{
    long ret;

    if (GCS_CONN_JOINER == conn->state) {
        ret = _release_sst_flow_control (conn);
        if (ret < 0) {
            gu_fatal ("Releasing SST flow control failed: %ld (%s)",
                      ret, strerror (-ret));
            abort();
        }
        conn->timeout = GU_TIME_ETERNITY;
    }

    /* See also gcs_handle_act_conf () for a case of cluster bootstrapping */
    if (gcs_shift_state (conn, GCS_CONN_JOINED)) {
        conn->fc_offset    = conn->queue_len;
        conn->need_to_join = false;
        gu_debug("Become joined, FC offset %ld", conn->fc_offset);
        /* One of the cases when the node can become SYNCED */
        if ((ret = gcs_send_sync (conn))) {
            gu_warn ("Sending SYNC failed: %ld (%s)", ret, strerror (-ret));
        }
    }
    else {
        assert (0);
    }
}

static void
gcs_become_synced (gcs_conn_t* conn)
{
    gcs_shift_state (conn, GCS_CONN_SYNCED);
    conn->sync_sent = false;
    gu_debug("Become synced, FC offset %ld", conn->fc_offset);
    conn->fc_offset = 0;
}

/* to be called under protection of both recv_q and fc_lock */
static void
_set_fc_limits (gcs_conn_t* conn)
{
    /* Killing two birds with one stone: flat FC profile for master-slave setups
     * plus #440: giving single node some slack at some math correctness exp.*/
    double fn = conn->params.fc_master_slave ? 1.0 : sqrt(conn->memb_num);

    conn->upper_limit = conn->params.fc_base_limit * fn + .5;
    conn->lower_limit = conn->upper_limit * conn->params.fc_resume_factor + .5;

    gu_info ("Flow-control interval: [%ld, %ld]",
             conn->lower_limit, conn->upper_limit);
}

/*! Handles flow control events
 *  (this is frequent, so leave it inlined) */
static inline void
gcs_handle_flow_control (gcs_conn_t*                conn,
                         const struct gcs_fc_event* fc)
{
    if (gtohl(fc->conf_id) != (uint32_t)conn->conf_id) {
        // obsolete fc request
        return;
    }

    conn->stop_count += ((fc->stop != 0) << 1) - 1; // +1 if !0, -1 if 0
    conn->stats_fc_received += (fc->stop != 0);

    if (1 == conn->stop_count) {
        gcs_sm_pause (conn->sm);    // first STOP request
    }
    else if (0 == conn->stop_count) {
        gcs_sm_continue (conn->sm); // last CONT request
    }

    return;
}

static void
_reset_pkt_size(gcs_conn_t* conn)
{
    if (conn->state != GCS_CONN_CLOSED) return; // #600 workaround

    long ret;

    if (0 > (ret = gcs_core_set_pkt_size (conn->core,
                                          conn->params.max_packet_size))) {
        gu_warn ("Failed to set packet size: %ld (%s)", ret, strerror(-ret));
    }
}

static long
_join (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    long err;

    while (-EAGAIN == (err = gcs_core_send_join (conn->core, seqno)))
        usleep (10000);

    switch (err)
    {
    case -ENOTCONN:
        gu_warn ("Sending JOIN failed: %d (%s). "
                 "Will retry in new primary component.", err, strerror(-err));
    case 0:
        return 0;
    default:
        gu_error ("Sending JOIN failed: %d (%s).", err, strerror(-err));
        return err;
    }
}

/*! Handles configuration action */
// TODO: this function does not provide any way for recv_thread to gracefully
//       exit in case of self-leave message.
static void
gcs_handle_act_conf (gcs_conn_t* conn, const void* action)
{
    const gcs_act_conf_t* conf = (const gcs_act_conf_t*)action;
    long ret;

    conn->my_idx = conf->my_idx;

    gu_fifo_lock(conn->recv_q);
    {
        /* reset flow control as membership is most likely changed */
        if (!gu_mutex_lock (&conn->fc_lock)) {
            conn->stop_sent   = 0;
            conn->stop_count  = 0;
            conn->conf_id     = conf->conf_id;
            conn->memb_num    = conf->memb_num;

            _set_fc_limits (conn);

            gu_mutex_unlock (&conn->fc_lock);
        }
        else {
            gu_fatal ("Failed to lock mutex.");
            abort();
        }

        conn->sync_sent = false;

        // need to wake up send monitor if it was paused during CC
        gcs_sm_continue(conn->sm);
    }
    gu_fifo_release (conn->recv_q);

    if (conf->conf_id < 0) {
        if (0 == conf->memb_num) {
            assert (conf->my_idx < 0);
            gu_info ("Received SELF-LEAVE. Closing connection.");
            gcs_shift_state (conn, GCS_CONN_CLOSED);
        }
        else {
            gu_info ("Received NON-PRIMARY.");
            assert (GCS_NODE_STATE_NON_PRIM == conf->my_state);
            gcs_become_open (conn);
            conn->global_seqno = conf->seqno;
        }

        return;
    }

    assert (conf->conf_id  >= 0);

    /* <sanity_checks> */
    if (conf->memb_num < 1) {
        gu_fatal ("Internal error: PRIMARY configuration with %d nodes",
                  conf->memb_num);
        abort();
    }

    if (conf->my_idx < 0 || conf->my_idx >= conf->memb_num) {
        gu_fatal ("Internal error: index of this node (%d) is out of bounds: "
                  "[%d, %d]", conf->my_idx, 0, conf->memb_num - 1);
        abort();
    }

    if (conf->my_state < GCS_NODE_STATE_PRIM) {
        gu_fatal ("Internal error: NON-PRIM node state in PRIM configuraiton");
        abort();
    }
    /* </sanity_checks> */

    conn->global_seqno = conf->seqno;

    /* at this point we have established protocol version,
     * so can set packet size */
// Ticket #600: commented out as unsafe under load    _reset_pkt_size(conn);

    const gcs_conn_state_t old_state = conn->state;
    switch (conf->my_state) {
    case GCS_NODE_STATE_PRIM:   gcs_become_primary(conn);      return;
        /* Below are not real state transitions, rather state recovery,
         * so bypassing state transition matrix */
    case GCS_NODE_STATE_JOINER: conn->state = GCS_CONN_JOINER; break;
    case GCS_NODE_STATE_DONOR:  conn->state = GCS_CONN_DONOR;  break;
    case GCS_NODE_STATE_JOINED: conn->state = GCS_CONN_JOINED; break;
    case GCS_NODE_STATE_SYNCED: conn->state = GCS_CONN_SYNCED; break;
    default:
        gu_fatal ("Internal error: unrecognized node state: %d",
                  conf->my_state);
        abort();
    }

    if (old_state != conn->state) {
        gu_info ("Restored state %s -> %s (%lld)",
                 gcs_conn_state_str[old_state], gcs_conn_state_str[conn->state],
                 conn->global_seqno);
    }

    switch (conn->state) {
    case GCS_CONN_JOINED:
        /* One of the cases when the node can become SYNCED */
    {
        bool send_sync = false;

        gu_fifo_lock(conn->recv_q);
        {
            send_sync = gcs_send_sync_begin(conn);
        }
        gu_fifo_release (conn->recv_q);

        if (send_sync && (ret = gcs_send_sync_end (conn))) {
            gu_warn ("CC: sending SYNC failed: %ld (%s)", ret, strerror (-ret));
        }
    }
    break;
    case GCS_CONN_JOINER:
    case GCS_CONN_DONOR:
        /* #603, #606 - duplicate JOIN msg in case we lost it */
        assert (conf->conf_id >= 0);

        if (conn->need_to_join) _join (conn, conn->join_seqno);

        break;
    default:
        break;
    }
}

static long
gcs_handle_act_state_req (gcs_conn_t*          conn,
                          struct gcs_act_rcvd* rcvd)
{
    if ((gcs_seqno_t)conn->my_idx == rcvd->id) {
        int const donor_idx = (int)rcvd->id; // to pacify valgrind
        gu_debug("Got GCS_ACT_STATE_REQ to %i, my idx: %ld",
                 donor_idx, conn->my_idx);
        // rewrite to pass global seqno for application
        rcvd->id = conn->global_seqno;
        return gcs_become_donor (conn);
    }
    else {
        if (rcvd->id >= 0) {
            gcs_become_joiner (conn);
        }
        return 1; // pass to gcs_request_state_transfer() caller.
    }
}

/*! Allocates buffer with malloc to pass to the upper layer. */
static long
gcs_handle_state_change (gcs_conn_t*           conn,
                         const struct gcs_act* act)
{
    gu_debug ("Got '%s' dated %lld", gcs_act_type_to_str (act->type),
              gcs_seqno_gtoh(*(gcs_seqno_t*)act->buf));

    void* buf = malloc (act->buf_len);

    if (buf) {
        memcpy (buf, act->buf, act->buf_len);
        /* initially act->buf points to internal static recv buffer. No leak here */
        ((struct gcs_act*)act)->buf = buf;
        return 1;
    }
    else {
        gu_fatal ("Could not allocate state change action (%zd bytes)",
                  act->buf_len);
        abort();
        return -ENOMEM;
    }
}

/*!
 * Performs work requred by action in current context.
 * @return negative error code, 0 if action should be discarded, 1 if should be
 *         passed to application.
 */
static long
gcs_handle_actions (gcs_conn_t*          conn,
                    struct gcs_act_rcvd* rcvd)
{
    long ret = 0;

    switch (rcvd->act.type) {
    case GCS_ACT_FLOW:
        assert (sizeof(struct gcs_fc_event) == rcvd->act.buf_len);
        gcs_handle_flow_control (conn, (const gcs_fc_event*)rcvd->act.buf);
        break;
    case GCS_ACT_CONF:
        gcs_handle_act_conf (conn, rcvd->act.buf);
        ret = 1;
        break;
    case GCS_ACT_STATE_REQ:
        ret = gcs_handle_act_state_req (conn, rcvd);
        break;
    case GCS_ACT_JOIN:
        ret = gcs_handle_state_change (conn, &rcvd->act);
        if (gcs_seqno_gtoh(*(gcs_seqno_t*)rcvd->act.buf) < 0 &&
            GCS_CONN_JOINER == conn->state)
            gcs_become_primary (conn);
        else
            gcs_become_joined (conn);
        break;
    case GCS_ACT_SYNC:
        ret = gcs_handle_state_change (conn, &rcvd->act);
        gcs_become_synced (conn);
        break;
    default:
        break;
    }

    return ret;
}

static inline void
GCS_FIFO_PUSH_TAIL (gcs_conn_t* conn, ssize_t size)
{
    conn->recv_q_size += size;
    gu_fifo_push_tail(conn->recv_q);
}

/* Returns true if timeout was handled and false otherwise */
static bool
_handle_timeout (gcs_conn_t* conn)
{
    bool ret;
    long long now = gu_time_calendar();

    /* TODO: now the only point for timeout is flow control (#412),
     *       later we might need to handle more timers. */
    if (conn->timeout <= now) {
        ret = ((GCS_CONN_JOINER != conn->state) ||
               (_release_sst_flow_control (conn) >= 0));
    }
    else {
        gu_error ("Unplanned timeout! (tout: %lld, now: %lld)",
                  conn->timeout, now);
        ret = false;
    }

    conn->timeout = GU_TIME_ETERNITY;

    return ret;
}

static long
_check_recv_queue_growth (gcs_conn_t* conn, ssize_t size)
{
    assert (GCS_CONN_JOINER == conn->state);

    long      ret   = 0;
    long long pause = gcs_fc_process (&conn->stfc, size);

    if (pause > 0) {
        /* replication needs throttling */
        if (conn->stop_sent <= 0) {
            if ((ret = gcs_send_fc_event (conn, GCS_FC_STOP)) >= 0) {
                conn->stop_sent++;
                ret = 0;
            }
            else {
                ret = gcs_check_error (ret, "Failed to send SST FC_STOP.");
            }
        }

        if (gu_likely(pause != GU_TIME_ETERNITY)) {

            if (GU_TIME_ETERNITY == conn->timeout) {
                conn->timeout = gu_time_calendar();
            }

            conn->timeout += pause; // we need to track pauses regardless
        }
        else if (conn->timeout != GU_TIME_ETERNITY) {
            conn->timeout = GU_TIME_ETERNITY;
            gu_warn ("Replication paused until state transfer is complete "
                     "due to reaching hard limit on the writeset queue size.");
        }

        return ret;
    }
    else {
        return pause; // 0 or error code
    }
}

static long
_close(gcs_conn_t* conn, bool join_recv_thread)
{
    /* all possible races in connection closing should be resolved by
     * the following call, it is thread-safe */

    long ret;

    if (gu_atomic_fetch_and_add(&conn->inner_close_count, 1) != 0) {
        return -EALREADY;
    }

    if (!(ret = gcs_sm_close (conn->sm))) {
        // we ignore return value on purpose. the reason is
        // we can not tell why self-leave message is generated.
        // there are two possible reasons.
        // 1. gcs_core_close is called.
        // 2. GCommConn::run() caught exception.
        (void)gcs_core_close (conn->core);

        if (join_recv_thread)
        {
            /* if called from gcs_close(), we need to synchronize with
               gcs_recv_thread at this point */
            if ((ret = gu_thread_join (conn->recv_thread, NULL))) {
                gu_error ("Failed to join recv_thread(): %d (%s)",
                          -ret, strerror(-ret));
            }
            else {
                gu_info ("recv_thread() joined.");
            }
            /* recv_thread() is supposed to set state to CLOSED when exiting */
            assert (GCS_CONN_CLOSED == conn->state);
        }

        gu_info ("Closing replication queue.");
        struct gcs_repl_act** act_ptr;
        /* At this point (state == CLOSED) no new threads should be able to
         * queue for repl (check gcs_repl()), and recv thread is joined, so no
         * new actions will be received. Abort threads that are still waiting
         * in repl queue */
        while ((act_ptr =
                (struct gcs_repl_act**)gcs_fifo_lite_get_head (conn->repl_q))) {
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

        /* wake all gcs_recv() threads () */
        // FIXME: this can block waiting for applicaiton threads to fetch all
        // items. In certain situations this can block forever. Ticket #113
        gu_info ("Closing slave action queue.");
        gu_fifo_close (conn->recv_q);
    }

    return ret;
}

/*
 * gcs_recv_thread() receives whatever actions arrive from group,
 * and performs necessary actions based on action type.
 */
static void *gcs_recv_thread (void *arg)
{
    gcs_conn_t* conn = (gcs_conn_t*)arg;
    ssize_t     ret  = -ECONNABORTED;

    // To avoid race between gcs_open() and the following state check in while()
    gu_cond_t tmp_cond; /* TODO: rework when concurrency in SM is allowed */
    gu_cond_init (&tmp_cond, NULL);
    gcs_sm_enter(conn->sm, &tmp_cond, false, true);
    gcs_sm_leave(conn->sm);
    gu_cond_destroy (&tmp_cond);

    while (conn->state < GCS_CONN_CLOSED)
    {
        gcs_seqno_t this_act_id = GCS_SEQNO_ILL;
        struct gcs_repl_act** repl_act_ptr;
        struct gcs_act_rcvd   rcvd;

        ret = gcs_core_recv (conn->core, &rcvd, conn->timeout);

        if (gu_unlikely(ret <= 0)) {

            if (-ETIMEDOUT == ret && _handle_timeout(conn)) continue;

            struct gcs_recv_act* err_act =
                (struct gcs_recv_act*) gu_fifo_get_tail(conn->recv_q);

            assert (NULL          == rcvd.act.buf);
            assert (0             == rcvd.act.buf_len);
            assert (GCS_ACT_ERROR == rcvd.act.type);
            assert (GCS_SEQNO_ILL == rcvd.id);

            err_act->rcvd     = rcvd;
            err_act->local_id = GCS_SEQNO_ILL;

            GCS_FIFO_PUSH_TAIL (conn, rcvd.act.buf_len);

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
        if (gu_likely (rcvd.act.type != GCS_ACT_TORDERED ||
                       (rcvd.id > 0 && (conn->global_seqno = rcvd.id)))) {
            /* successful delivery - increment local order */
            this_act_id = gu_atomic_fetch_and_add(&conn->local_act_id, 1);
        }

        if (NULL != rcvd.local                                          &&
            (repl_act_ptr = (struct gcs_repl_act**)
             gcs_fifo_lite_get_head (conn->repl_q))                     &&
            (gu_likely ((*repl_act_ptr)->act_in == rcvd.local)  ||
             /* at this point repl_q is locked and we need to unlock it and
              * return false to fall to the 'else' branch; unlikely case */
             (gcs_fifo_lite_release (conn->repl_q), false)))
        {
            /* local action from repl_q */
            struct gcs_repl_act* repl_act = *repl_act_ptr;
            gcs_fifo_lite_pop_head (conn->repl_q);

            assert (repl_act->action->type == rcvd.act.type);
            assert (repl_act->action->size == rcvd.act.buf_len ||
                    repl_act->action->type == GCS_ACT_STATE_REQ);

            repl_act->action->buf     = rcvd.act.buf;
            repl_act->action->seqno_g = rcvd.id;
            repl_act->action->seqno_l = this_act_id;

            gu_mutex_lock   (&repl_act->wait_mutex);
            gu_cond_signal  (&repl_act->wait_cond);
            gu_mutex_unlock (&repl_act->wait_mutex);
        }
        else if (gu_likely(this_act_id >= 0))
        {
            /* remote/non-repl'ed action */
            struct gcs_recv_act* recv_act =
                (struct gcs_recv_act*)gu_fifo_get_tail (conn->recv_q);

            if (gu_likely (NULL != recv_act)) {

                recv_act->rcvd     = rcvd;
                recv_act->local_id = this_act_id;

                conn->queue_len = gu_fifo_length (conn->recv_q) + 1;
                bool send_stop  = gcs_fc_stop_begin (conn);

                // release queue
                GCS_FIFO_PUSH_TAIL (conn, rcvd.act.buf_len);

                if (gu_unlikely(GCS_CONN_JOINER == conn->state)) {
                    ret = _check_recv_queue_growth (conn, rcvd.act.buf_len);
                    assert (ret <= 0);
                    if (ret < 0) break;
                }

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
            gu_fatal("Protocol violation: unordered local action not in repl_q:"
                     " { {%p, %zd, %s}, %ld, %lld }.",
                     rcvd.act.buf, rcvd.act.buf_len,
                     gcs_act_type_to_str(rcvd.act.type), rcvd.sender_idx,
                     rcvd.id);
            assert(0);
            ret = -ENOTRECOVERABLE;
            break;
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

    if (ret > 0) {
        ret = 0;
    }
    else if (ret < 0)
    {
        /* In case of error call _close() to release repl_q waiters. */
        (void)_close(conn, false);
        gcs_shift_state (conn, GCS_CONN_CLOSED);
    }
    gu_info ("RECV thread exiting %d: %s", ret, strerror(-ret));
    return NULL;
}

/* Opens connection to group */
long gcs_open (gcs_conn_t* conn, const char* channel, const char* url,
               bool const bootstrap)
{
    long ret = 0;

    if ((ret = gcs_sm_open(conn->sm))) return ret; // open in case it is closed

    gu_cond_t tmp_cond; /* TODO: rework when concurrency in SM is allowed */
    gu_cond_init (&tmp_cond, NULL);

    if ((ret = gcs_sm_enter (conn->sm, &tmp_cond, false, true)))
    {
        gu_error("Failed to enter send monitor: %d (%s)", ret, strerror(-ret));
        return ret;
    }

    if (GCS_CONN_CLOSED == conn->state) {

        if (!(ret = gcs_core_open (conn->core, channel, url, bootstrap))) {

            _reset_pkt_size(conn);

            if (!(ret = gu_thread_create (&conn->recv_thread, NULL,
                                          gcs_recv_thread, conn))) {
                gcs_fifo_lite_open(conn->repl_q);
                gu_fifo_open(conn->recv_q);
                gcs_shift_state (conn, GCS_CONN_OPEN);
                gu_info ("Opened channel '%s'", channel);
                conn->inner_close_count = 0;
                conn->outer_close_count = 0;
                goto out;
            }
            else {
                gu_error ("Failed to create main receive thread: %ld (%s)",
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
        gu_error ("Bad GCS connection state: %d (%s)",
                  conn->state, gcs_conn_state_str[conn->state]);
        ret = -EBADFD;
    }
out:
    gcs_sm_leave (conn->sm);
    gu_cond_destroy (&tmp_cond);

    return ret;
}

/* Closes group connection */
/* After it returns, application should have all time in the world to cancel
 * and join threads which try to access the handle, before calling gcs_destroy()
 * on it. */
long gcs_close (gcs_conn_t *conn)
{
    long ret;

    if (gu_atomic_fetch_and_add(&conn->outer_close_count, 1) != 0) {
        return -EALREADY;
    }

    if ((ret = _close(conn, true)) == -EALREADY)
    {
        gu_info("recv_thread() already closing, joining thread.");
        /* _close() has already been called by gcs_recv_thread() and it
           is taking care of cleanup, just join the thread */
        if ((ret = gu_thread_join (conn->recv_thread, NULL))) {
            gu_error ("Failed to join recv_thread(): %d (%s)",
                      -ret, strerror(-ret));
        }
        else {
            gu_info ("recv_thread() joined.");
        }
    }
    /* recv_thread() is supposed to set state to CLOSED when exiting */
    assert (GCS_CONN_CLOSED == conn->state);
    return ret;
}

/* Frees resources associated with GCS connection handle */
long gcs_destroy (gcs_conn_t *conn)
{
    long err;

    gu_cond_t tmp_cond;
    gu_cond_init (&tmp_cond, NULL);

    if ((err = gcs_sm_enter (conn->sm, &tmp_cond, false, true))) // need an error here
    {
        if (GCS_CONN_CLOSED != conn->state)
        {
            if (GCS_CONN_CLOSED > conn->state)
                gu_error ("Attempt to call gcs_destroy() before gcs_close(): "
                          "state = %d", conn->state);

            gu_cond_destroy (&tmp_cond);

            return -EBADFD;
        }

        /* this should cancel all recv calls */
        gu_fifo_destroy (conn->recv_q);

        gcs_shift_state (conn, GCS_CONN_DESTROYED);
//DELETE        conn->err   = -EBADFD;
        /* we must unlock the mutex here to allow unfortunate threads
         * to acquire the lock and give up gracefully */
    }
    else {
        gcs_sm_leave (conn->sm);
        gu_cond_destroy (&tmp_cond);
        err = -EBADFD;
        return err;
    }

    gu_cond_destroy (&tmp_cond);
    gcs_sm_destroy (conn->sm);

    if ((err = gcs_fifo_lite_destroy (conn->repl_q))) {
        gu_debug ("Error destroying repl FIFO: %d (%s)", err, strerror(-err));
        return err;
    }

    if ((err = gcs_core_destroy (conn->core))) {
        gu_debug ("Error destroying core: %d (%s)", err, strerror(-err));
        return err;
    }

    /* This must not last for long */
    while (gu_mutex_destroy (&conn->fc_lock));

    _cleanup_params (conn);

    gu_free (conn);

    return 0;
}

/* Puts action in the send queue and returns */
long gcs_sendv (gcs_conn_t*          const conn,
                const struct gu_buf* const act_bufs,
                size_t               const act_size,
                gcs_act_type_t       const act_type,
                bool                 const scheduled)
{
    if (gu_unlikely(act_size > GCS_MAX_ACT_SIZE)) return -EMSGSIZE;

    long ret = -ENOTCONN;

    /*! locking connection here to avoid race with gcs_close()
     *  @note: gcs_repl() and gcs_recv() cannot lock connection
     *         because they block indefinitely waiting for actions */
    gu_cond_t tmp_cond;
    gu_cond_init (&tmp_cond, NULL);

    if (!(ret = gcs_sm_enter (conn->sm, &tmp_cond, scheduled, true)))
    {
        while ((GCS_CONN_OPEN >= conn->state) &&
               (ret = gcs_core_send (conn->core, act_bufs,
                                     act_size, act_type)) == -ERESTART);
        gcs_sm_leave (conn->sm);
        gu_cond_destroy (&tmp_cond);
    }

    return ret;
}

long gcs_schedule (gcs_conn_t* conn)
{
    return gcs_sm_schedule (conn->sm);
}

long gcs_interrupt (gcs_conn_t* conn, long handle)
{
    return gcs_sm_interrupt (conn->sm, handle);
}

gcs_seqno_t gcs_caused(gcs_conn_t* conn)
{
    return gcs_core_caused(conn->core);
}

/* Puts action in the send queue and returns after it is replicated */
long gcs_replv (gcs_conn_t*          const conn,      //!<in
                const struct gu_buf* const act_in,    //!<in
                struct gcs_action*   const act,       //!<inout
                bool                 const scheduled) //!<in
{
    if (gu_unlikely((size_t)act->size > GCS_MAX_ACT_SIZE)) return -EMSGSIZE;

    long ret;

    assert (act);
    assert (act->size > 0);

    act->seqno_l = GCS_SEQNO_ILL;
    act->seqno_g = GCS_SEQNO_ILL;

    /* This is good - we don't have to do a copy because we wait */
    struct gcs_repl_act repl_act(act_in, act);

    gu_mutex_init (&repl_act.wait_mutex, NULL);
    gu_cond_init  (&repl_act.wait_cond,  NULL);

    /* Send action and wait for signal from recv_thread
     * we need to lock a mutex before we can go wait for signal */
    if (!(ret = gu_mutex_lock (&repl_act.wait_mutex)))
    {
        // Lock here does the following:
        // 1. serializes gcs_core_send() access between gcs_repl() and
        //    gcs_send()
        // 2. avoids race with gcs_close() and gcs_destroy()
        if (!(ret = gcs_sm_enter (conn->sm, &repl_act.wait_cond, scheduled, true)))
        {
            struct gcs_repl_act** act_ptr;

//#ifndef NDEBUG
            const void* const orig_buf = act->buf;
//#endif

            // some hack here to achieve one if() instead of two:
            // ret = -EAGAIN part is a workaround for #569
            // if (conn->state >= GCS_CONN_CLOSE) or (act_ptr == NULL)
            // ret will be -ENOTCONN
            if ((ret = -EAGAIN,
                 conn->upper_limit >= conn->queue_len ||
                 act->type         != GCS_ACT_TORDERED)         &&
                (ret = -ENOTCONN, GCS_CONN_OPEN >= conn->state) &&
                (act_ptr = (struct gcs_repl_act**)gcs_fifo_lite_get_tail (conn->repl_q)))
            {
                *act_ptr = &repl_act;
                gcs_fifo_lite_push_tail (conn->repl_q);

                // Keep on trying until something else comes out
                while ((ret = gcs_core_send (conn->core, act_in, act->size,
                                             act->type)) == -ERESTART) {}

                if (ret < 0) {
                    /* remove item from the queue, it will never be delivered */
                    gu_warn ("Send action {%p, %zd, %s} returned %d (%s)",
                             act->buf, act->size,gcs_act_type_to_str(act->type),
                             ret, strerror(-ret));

                    if (!gcs_fifo_lite_remove (conn->repl_q)) {
                        gu_fatal ("Failed to remove unsent item from repl_q");
                        assert(0);
                        ret = -ENOTRECOVERABLE;
                    }
                }
                else {
                    assert (ret == (ssize_t)act->size);
                }
            }

            gcs_sm_leave (conn->sm);

            assert(ret);

            /* now we can go waiting for action delivery */
            if (ret >= 0) {
                gu_cond_wait (&repl_act.wait_cond, &repl_act.wait_mutex);
#ifndef GCS_FOR_GARB
                /* assert (act->buf != 0); */
                if (act->buf == 0)
                {
                    /* Recv thread purged repl_q before action was delivered */
                    ret = -ENOTCONN;
                    goto out;
                }
#else
                assert (act->buf == 0);
#endif /* GCS_FOR_GARB */

                if (act->seqno_g < 0) {
                    assert (GCS_SEQNO_ILL    == act->seqno_l ||
                            GCS_ACT_TORDERED != act->type);

                    if (act->seqno_g == GCS_SEQNO_ILL) {
                        /* action was not replicated for some reason */
                        assert (orig_buf == act->buf);
                        ret = -EINTR;
                    }
                    else {
                        /* core provided an error code in global seqno */
                        assert (orig_buf != act->buf);
                        ret = act->seqno_g;
                        act->seqno_g = GCS_SEQNO_ILL;
                    }

                    if (orig_buf != act->buf) // action was allocated in gcache
                    {
                        gu_debug("Freeing gcache buffer %p after receiving %d",
                                 act->buf, ret);
                        gcs_gcache_free (conn->gcache, act->buf);
                        act->buf = orig_buf;
                    }
                }
            }
        }
#ifndef GCS_FOR_GARB
    out:
#endif /* GCS_FOR_GARB */
        gu_mutex_unlock  (&repl_act.wait_mutex);
    }
    gu_mutex_destroy (&repl_act.wait_mutex);
    gu_cond_destroy  (&repl_act.wait_cond);

#ifdef GCS_DEBUG_GCS
//    gu_debug ("\nact_size = %u\nact_type = %u\n"
//              "act_id   = %llu\naction   = %p (%s)\n",
//              act->size, act->type, act->seqno_g, act->buf, act->buf);
#endif
    return ret;
}

long gcs_request_state_transfer (gcs_conn_t  *conn,
                                 int          version,
                                 const void  *req,
                                 size_t       size,
                                 const char  *donor,
                                 const gu_uuid_t* ist_uuid,
                                 gcs_seqno_t ist_seqno,
                                 gcs_seqno_t *local)
{
    long   ret       = -ENOMEM;
    size_t donor_len = strlen(donor) + 1; // include terminating \0
    size_t rst_size  = size + donor_len + sizeof(*ist_uuid) + sizeof(ist_seqno) + 2;
    // for simplicity, allocate maximum space what we need here.
    char*  rst       = (char*)gu_malloc (rst_size);

    *local = GCS_SEQNO_ILL;

    if (rst) {
        gu_debug("ist_uuid[" GU_UUID_FORMAT "], ist_seqno[%lld]",
                 GU_UUID_ARGS(ist_uuid), (long long)ist_seqno);

        int offset = 0;

        // version 0,1
        /* RST format: |donor name|\0|app request|
         * anything more complex will require a special (de)serializer.
         * NOTE: this is sender part. Check gcs_group_handle_state_request()
         *       for the receiver part. */

        if (version < 2) {
            memcpy (rst + offset, donor, donor_len);
            offset += donor_len;
            memcpy (rst + offset, req, size);
            rst_size = size + donor_len;
        }

        // version 2(expose joiner's seqno and smart donor selection)
        // RST format: |donor_name|\0|'V'|version|ist_uuid|ist_seqno|app_request|

        // we expect 'version' could be hold by 'char'
        // since app_request v0 starts with sst method name
        // and app_request v1 starts with 'STRv1'
        // and ist_uuid starts with hex character in lower case.
        // it's safe to use 'V' as separator.
        else {
            memcpy (rst + offset, donor, donor_len);
            offset += donor_len;
            rst[offset++] = 'V';
            rst[offset++] = (char)version;
            memcpy (rst + offset, ist_uuid, sizeof(*ist_uuid));
            offset += sizeof(*ist_uuid);
            *(gcs_seqno_t*) (rst + offset) = gcs_seqno_htog(ist_seqno);
            offset += sizeof(ist_seqno);
            memcpy (rst + offset, req, size);
        }

        struct gcs_action action;
        action.buf  = rst;
        action.size = (ssize_t)rst_size;
        action.type = GCS_ACT_STATE_REQ;

        ret = gcs_repl(conn, &action, false);

        gu_free (rst);

        *local = action.seqno_l;

        if (ret > 0) {
            assert (action.buf != rst);
#ifndef GCS_FOR_GARB
            assert (action.buf != NULL);
            gcs_gcache_free (conn->gcache, action.buf);
#else
            assert (action.buf == NULL);
#endif
            assert (ret == (ssize_t)rst_size);
            assert (action.seqno_g >= 0);
            assert (action.seqno_l >  0);

            // on joiner global seqno stores donor index
            // on donor global seqno stores global seqno
            ret = action.seqno_g;
        }
        else {
            assert (/*action.buf == NULL ||*/ action.buf == rst);
        }
    }

    return ret;
}

long gcs_desync (gcs_conn_t* conn, gcs_seqno_t* local)
{
    gu_uuid_t ist_uuid = {{0, }};
    gcs_seqno_t ist_seqno = GCS_SEQNO_ILL;
    // for desync operation we use the lowest str_version.
    long ret = gcs_request_state_transfer (conn, 0,
                                           "", 1, GCS_DESYNC_REQ,
                                           &ist_uuid, ist_seqno,
                                           local);

    if (ret >= 0) {
        return 0;
    }
    else {
        return ret;
    }
}

static inline void
GCS_FIFO_POP_HEAD (gcs_conn_t* conn, ssize_t size)
{
    assert (conn->recv_q_size >= size);
    conn->recv_q_size -= size;
    gu_fifo_pop_head (conn->recv_q);
}

/* Returns when an action from another process is received */
long gcs_recv (gcs_conn_t*        conn,
               struct gcs_action* action)
{
    int                  err;
    struct gcs_recv_act* recv_act = NULL;

    assert (action);

    if ((recv_act = (struct gcs_recv_act*)gu_fifo_get_head (conn->recv_q, &err)))
    {
        conn->queue_len = gu_fifo_length (conn->recv_q) - 1;
        bool send_cont  = gcs_fc_cont_begin   (conn);
        bool send_sync  = gcs_send_sync_begin (conn);

        action->buf     = (void*)recv_act->rcvd.act.buf;
        action->size    = recv_act->rcvd.act.buf_len;
        action->type    = recv_act->rcvd.act.type;
        action->seqno_g = recv_act->rcvd.id;
        action->seqno_l = recv_act->local_id;

        if (gu_unlikely (GCS_ACT_CONF == action->type)) {
            err = gu_fifo_cancel_gets (conn->recv_q);
            if (err) {
                gu_fatal ("Internal logic error: failed to cancel recv_q "
                          "\"gets\": %d (%s). Aborting.",
                          err, strerror(-err));
                gu_abort();
            }
        }

        GCS_FIFO_POP_HEAD (conn, action->size); // release the queue

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
                          "%d (%s). Aborting to avoid cluster lock-up...",
                          err, strerror(-err));
                gcs_close(conn);
                gu_abort();
            }
        }
        else if (gu_unlikely(send_sync) && (err = gcs_send_sync_end (conn))) {
            gu_warn ("Failed to send SYNC message: %d (%s). Will try later.",
                     err, strerror(-err));
        }

        return action->size;
    }
    else {
        action->buf     = NULL;
        action->size    = 0;
        action->type    = GCS_ACT_ERROR;
        action->seqno_g = GCS_SEQNO_ILL;
        action->seqno_l = GCS_SEQNO_ILL;

        switch (err) {
        case -ENODATA:
            assert (GCS_CONN_CLOSED == conn->state);
            return GCS_CLOSED_ERROR;
        default:
            return err;
        }
    }
}

long
gcs_resume_recv (gcs_conn_t* conn)
{
    int ret = GCS_CLOSED_ERROR;

    ret = gu_fifo_resume_gets (conn->recv_q);

    if (ret) {
        if (conn->state < GCS_CONN_CLOSED) {
            gu_fatal ("Internal logic error: failed to resume \"gets\" on "
                      "recv_q: %d (%s). Aborting.", ret, strerror (-ret));
            gcs_close (conn);
            gu_abort();
        }
        else {
            ret = GCS_CLOSED_ERROR;
        }
    }

    return ret;
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
            return -EAGAIN; // wait until get sync
        }
    }
}

long
gcs_conf_set_pkt_size (gcs_conn_t *conn, long pkt_size)
{
    if (conn->params.max_packet_size == pkt_size) return pkt_size;

    return gcs_set_pkt_size (conn, pkt_size);
}

long
gcs_set_last_applied (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    long ret = gcs_sm_enter (conn->sm, &cond, false, false);

    if (!ret) {
        ret = gcs_core_set_last_applied (conn->core, seqno);
        gcs_sm_leave (conn->sm);
    }

    gu_cond_destroy (&cond);

    return ret;
}

long
gcs_join (gcs_conn_t* conn, gcs_seqno_t seqno)
{
    conn->join_seqno   = seqno;
    conn->need_to_join = true;

    return _join (conn, seqno);
}

gcs_seqno_t gcs_local_sequence(gcs_conn_t* conn)
{
    return gu_atomic_fetch_and_add(&conn->local_act_id, 1);
}

void
gcs_get_stats (gcs_conn_t* conn, struct gcs_stats* stats)
{
    gu_fifo_stats_get (conn->recv_q,
                       &stats->recv_q_len,
                       &stats->recv_q_len_max,
                       &stats->recv_q_len_min,
                       &stats->recv_q_len_avg);

    stats->recv_q_size = conn->recv_q_size;

    gcs_sm_stats_get (conn->sm,
                      &stats->send_q_len,
                      &stats->send_q_len_max,
                      &stats->send_q_len_min,
                      &stats->send_q_len_avg,
                      &stats->fc_paused_ns,
                      &stats->fc_paused_avg);

    stats->fc_sent     = conn->stats_fc_sent;
    stats->fc_received = conn->stats_fc_received;
}

void
gcs_flush_stats(gcs_conn_t* conn)
{
    gu_fifo_stats_flush(conn->recv_q);
    gcs_sm_stats_flush (conn->sm);
    conn->stats_fc_sent     = 0;
    conn->stats_fc_received = 0;
}

void gcs_get_status(gcs_conn_t* conn, gu::Status& status)
{
    if (conn->state < GCS_CONN_CLOSED)
    {
        gcs_core_get_status(conn->core, status);
    }
}

static long
_set_fc_limit (gcs_conn_t* conn, const char* value)
{
    long long limit;
    const char* const endptr = gu_str2ll(value, &limit);

    if (limit > 0LL && *endptr == '\0') {

        if (limit > LONG_MAX) limit = LONG_MAX;

        gu_fifo_lock(conn->recv_q);
        {
            if (!gu_mutex_lock (&conn->fc_lock)) {
                conn->params.fc_base_limit = limit;
                _set_fc_limits (conn);
                gu_config_set_int64 (conn->config, GCS_PARAMS_FC_LIMIT,
                                     conn->params.fc_base_limit);
                gu_mutex_unlock (&conn->fc_lock);
            }
            else {
                gu_fatal ("Failed to lock mutex.");
                abort();
            }
        }
        gu_fifo_release (conn->recv_q);

        return 0;
    }
    else {
        return -EINVAL;
    }
}

static long
_set_fc_factor (gcs_conn_t* conn, const char* value)
{
    double factor;
    const char* const endptr = gu_str2dbl(value, &factor);

    if (factor >= 0.0 && factor <= 1.0 && *endptr == '\0') {

        if (factor == conn->params.fc_resume_factor) return 0;

        gu_fifo_lock(conn->recv_q);
        {
            if (!gu_mutex_lock (&conn->fc_lock)) {
                conn->params.fc_resume_factor = factor;
                _set_fc_limits (conn);
                gu_config_set_double (conn->config, GCS_PARAMS_FC_FACTOR,
                                      conn->params.fc_resume_factor);
                gu_mutex_unlock (&conn->fc_lock);
            }
            else {
                gu_fatal ("Failed to lock mutex.");
                abort();
            }
        }
        gu_fifo_release (conn->recv_q);

        return 0;
    }
    else {
        return -EINVAL;
    }
}

static long
_set_fc_debug (gcs_conn_t* conn, const char* value)
{
    bool debug;
    const char* const endptr = gu_str2bool(value, &debug);

    if (*endptr == '\0') {

        if (conn->params.fc_debug == debug) return 0;

        conn->params.fc_debug = debug;
        gcs_fc_debug (&conn->stfc, debug);
        gu_config_set_bool (conn->config, GCS_PARAMS_FC_DEBUG, debug);

        return 0;
    }
    else {
        return -EINVAL;
    }
}

static long
_set_sync_donor (gcs_conn_t* conn, const char* value)
{
    bool sd;
    const char* const endptr = gu_str2bool (value, &sd);

    if (endptr[0] != '\0') return -EINVAL;

    if (conn->params.sync_donor != sd) {

        conn->params.sync_donor = sd;
        conn->max_fc_state      = sd ? GCS_CONN_DONOR : GCS_CONN_JOINED;
    }

    return 0;
}

static long
_set_pkt_size (gcs_conn_t* conn, const char* value)
{
    long long pkt_size;
    const char* const endptr = gu_str2ll (value, &pkt_size);

    if (pkt_size > 0 && *endptr == '\0') {

        if (pkt_size > LONG_MAX) pkt_size = LONG_MAX;

        if (conn->params.max_packet_size == pkt_size) return 0;

        long ret = gcs_set_pkt_size (conn, pkt_size);

        if (ret >= 0)
        {
            ret = 0;
            gu_config_set_int64(conn->config,GCS_PARAMS_MAX_PKT_SIZE,pkt_size);
        }

        return ret;
    }
    else {
//        gu_warn ("Invalid value for %s: '%s'", GCS_PARAMS_PKT_SIZE, value);
        return -EINVAL;
    }
}

static long
_set_recv_q_hard_limit (gcs_conn_t* conn, const char* value)
{
    long long limit;
    const char* const endptr = gu_str2ll (value, &limit);

    if (limit > 0 && *endptr == '\0') {

        if (limit > LONG_MAX) limit = LONG_MAX;

        long long limit_fixed = limit * gcs_fc_hard_limit_fix;

        if (conn->params.recv_q_hard_limit == limit_fixed) return 0;

        gu_config_set_int64 (conn->config, GCS_PARAMS_RECV_Q_HARD_LIMIT, limit);
        conn->params.recv_q_hard_limit = limit_fixed;

        return 0;
    }
    else {
        return -EINVAL;
    }
}

static long
_set_recv_q_soft_limit (gcs_conn_t* conn, const char* value)
{
    double dbl;
    const char* const endptr = gu_str2dbl (value, &dbl);

    if (dbl >= 0.0 && dbl < 1.0 && *endptr == '\0') {

        if (dbl == conn->params.recv_q_soft_limit) return 0;

        gu_config_set_double (conn->config, GCS_PARAMS_RECV_Q_SOFT_LIMIT, dbl);
        conn->params.recv_q_soft_limit = dbl;

        return 0;
    }
    else {
        return -EINVAL;
    }
}

static long
_set_max_throttle (gcs_conn_t* conn, const char* value)
{
    double dbl;
    const char* const endptr = gu_str2dbl (value, &dbl);

    if (dbl >= 0.0 && dbl < 1.0 && *endptr == '\0') {

        if (dbl == conn->params.max_throttle) return 0;

        gu_config_set_double (conn->config, GCS_PARAMS_MAX_THROTTLE, dbl);
        conn->params.max_throttle = dbl;

        return 0;
    }
    else {
        return -EINVAL;
    }
}

bool gcs_register_params (gu_config_t* const conf)
{
    return (gcs_params_register (conf) | gcs_core_register (conf));
}

long gcs_param_set  (gcs_conn_t* conn, const char* key, const char *value)
{
    if (!strcmp (key, GCS_PARAMS_FC_LIMIT)) {
        return _set_fc_limit (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_FC_FACTOR)) {
        return _set_fc_factor (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_FC_DEBUG)) {
        return _set_fc_debug (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_SYNC_DONOR)) {
        return _set_sync_donor (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_MAX_PKT_SIZE)) {
        return _set_pkt_size (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_RECV_Q_HARD_LIMIT)) {
        return _set_recv_q_hard_limit (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_RECV_Q_SOFT_LIMIT)) {
        return _set_recv_q_soft_limit (conn, value);
    }
    else if (!strcmp (key, GCS_PARAMS_MAX_THROTTLE)) {
        return _set_max_throttle (conn, value);
    }
    else {
        return gcs_core_param_set (conn->core, key, value);
    }
}

const char* gcs_param_get (gcs_conn_t* conn, const char* key)
{
    gu_warn ("Not implemented: %s", __FUNCTION__);

    return NULL;
}
