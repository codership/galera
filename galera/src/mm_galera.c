// Copyright (C) 2007-2009 Codership Oy <info@codership.com>

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "wsrep_api.h"

#include <galerautils.h>
#include <gcs.h>
#include <wsdb_api.h>

#include "galera_job_queue.h"
#include "galera_info.h"
#include "galera_state.h"
#include "galera_options.h"
#include "galera_status.h"

#define GALERA_WORKAROUND_197   1 //w/around for #197: lack of cert db on joiner
#define GALERA_USE_FLOW_CONTROL 1
#define GALERA_USLEEP_FLOW_CONTROL    10000 //  0.01 sec
#define GALERA_USLEEP_1_SECOND      1000000 //  1 sec
#define GALERA_USLEEP_10_SECONDS   10000000 // 10 sec

// for pretty printing in diagnostic messages
static const char* galera_act_type_str[] =
{   "GCS_ACT_TORDERED",
    "GCS_ACT_COMMIT_CUT",
    "GCS_ACT_STATE_REQ",
    "GCS_ACT_CONF",
    "GCS_ACT_JOIN",
    "GCS_ACT_SYNC",
    "GCS_ACT_FLOW",
    "GCS_ACT_SERVICE",
    "GCS_ACT_ERROR",
    "GCS_ACT_UNKNOWN"
};

typedef enum galera_repl_state {
    GALERA_UNINITIALIZED,
    GALERA_INITIALIZED,
    GALERA_CONNECTED,
} galera_repl_state_t;

/* application's handlers */
static wsrep_bf_apply_cb_t      bf_apply_cb        = NULL;
static wsrep_ws_start_cb_t      ws_start_cb        = NULL;
static wsrep_view_cb_t          view_handler_cb    = NULL;
static wsrep_sst_prepare_cb_t   sst_prepare_cb     = NULL;
static wsrep_sst_donate_cb_t    sst_donate_cb      = NULL;

/* gcs parameters */
static gu_to_t            *cert_queue   = NULL;
static gu_to_t            *commit_queue = NULL;
static gcs_conn_t         *gcs_conn     = NULL;

struct galera_status status =
{
    { { 0 } },
    WSREP_SEQNO_UNDEFINED,
    0, 0, 0, 0, 0, 0, 0, 0,
    GALERA_STAGE_INIT
};

/* state trackers */
static galera_repl_state_t conn_state = GALERA_UNINITIALIZED;
static gcs_seqno_t         sst_seqno;         // received in state transfer
static gu_uuid_t           sst_uuid;          // received in state transfer
static gcs_seqno_t         last_recved  = -1; // last received from group
static long                my_idx = 0;

static struct job_queue   *applier_queue = NULL;

static const char* data_dir  = NULL;
static const char* sst_donor = NULL;

static gu_mutex_t commit_mtx;
static gu_mutex_t sst_mtx;
static gu_cond_t  sst_cond;
static gu_mutex_t close_mtx;
static gu_cond_t  close_cond;

static struct galera_options galera_opts;

//#define EXTRA_DEBUG
#ifdef EXTRA_DEBUG
static FILE *wslog_L;
static FILE *wslog_G;
#endif

/* @struct contains one write set and its TO sequence number
 */
struct job_context {
    enum job_type          type;  //!< job's nature
    trx_seqno_t            seqno; //!< seqno for the job
    struct wsdb_write_set *ws;    //!< write set to be applied
};

// a wrapper to re-try functions which can return -EAGAIN
static inline long while_eagain (
    long (*function) (gu_to_t*, gcs_seqno_t), gu_to_t* to, gcs_seqno_t seqno
) {
    static const struct timespec period = { 0, 10000000 }; // 10 msec
    long rcode;

    while (-EAGAIN == (rcode = function (to, seqno))) {
        nanosleep (&period, NULL);
    }

    return rcode;
}

static inline long while_eagain_or_trx_abort (
    wsrep_trx_id_t trx_id, long (*function) (gu_to_t*, gcs_seqno_t), 
    gu_to_t* to, gcs_seqno_t seqno
) {
    static const struct timespec period = { 0, 10000000 }; // 10 msec
    long rcode;

    while (-EAGAIN == (rcode = function (to, seqno))) {
        wsdb_trx_info_t info;
        nanosleep (&period, NULL);
        wsdb_get_local_trx_info(trx_id, &info);
        if (info.state == WSDB_TRX_MUST_ABORT) {
            gu_debug("WSDB_TRX_MUST_ABORT for trx: %lld %lld", info.seqno_g, info.seqno_l);
            return -EINTR;
        }
        gu_debug("INTRERRUPT for trx: %lld %lld in state: %d", info.seqno_g, info.seqno_l, info.state);
        return -EINTR;
    }

    return rcode;
}

#define GALERA_QUEUE_NAME(queue)                                \
    ((queue) == cert_queue ? "cert_queue" : "commit_queue")

// the following are made as macros to allow for correct line number reporting
#define GALERA_GRAB_QUEUE(queue, seqno)                                 \
    {                                                                   \
        long ret = while_eagain (gu_to_grab, queue, seqno);             \
        if (gu_unlikely(ret)) {                                         \
            gu_fatal("Failed to grab %s at %lld: %ld (%s)",             \
                     GALERA_QUEUE_NAME(queue), seqno, ret, strerror(-ret)); \
            assert(0);                                                  \
            abort();                                                    \
        }                                                               \
    }

#define GALERA_RELEASE_QUEUE(queue, seqno)                              \
    {                                                                   \
        long ret = gu_to_release (queue, seqno);                        \
        if (gu_unlikely(ret)) {                                         \
            gu_fatal("Failed to release %s at %lld: %ld (%s)",          \
                     GALERA_QUEUE_NAME(queue), seqno, ret, strerror(-ret)); \
            assert(0);                                                  \
            abort();                                                    \
        }                                                               \
    }

#define GALERA_SELF_CANCEL_QUEUE(queue, seqno)                          \
    {                                                                   \
        long ret = while_eagain (gu_to_self_cancel, queue, seqno);      \
        if (gu_unlikely(ret)) {                                         \
            gu_fatal("Failed to self-cancel %s at %lld: %ld (%s)",      \
                     GALERA_QUEUE_NAME(queue), seqno, ret, strerror(-ret)); \
            assert(0);                                                  \
            abort();                                                    \
        }                                                               \
    }

static int ws_conflict_check(void *ctx1, void *ctx2) {
    struct job_context *job1 = (struct job_context *)ctx1;
    struct job_context *job2 = (struct job_context *)ctx2;

    gu_debug("conflict check for: job1: %lld type: %d job2: %lld type %d",
             job1->seqno, job1->type, job2->seqno, job2->type
    );

    if (job1->seqno < job2->seqno) return 0;

    assert(job1->seqno > job2->seqno);

    if ((job2->type == JOB_TO_ISOLATION) ||
       (job2->type == JOB_REPLAYING)
    ) {
        gu_debug("job2 is brute force");
        return 1;
    }

    /* job1 is sequenced after job2, must check if they conflict */

    if (job1->ws->last_seen_trx >= job2->seqno)
    {
      trx_seqno_t last_seen_saved = job1->ws->last_seen_trx;
      int rcode;

      /* serious mis-use of certification test
       * we mangle ws seqno's so that certification_test certifies
       * against just only the job2 ws.
       * If somebody cares to modify wsdb_certification_test, it might
       * break this logic => take care
       */

      job1->ws->last_seen_trx = job2->seqno - 1;
      /* @todo: this will conflict with purging, need to use certification_mtx
       */
      rcode = wsdb_certification_test(job1->ws, (job2->seqno + 1), true); 

      job1->ws->last_seen_trx = last_seen_saved;
      if (rcode) {
          return 1;
      }
    }
    gu_debug("job1 qualified");

    return 0;
}

/*
 * @brief compare seqno order of two applying jobs
 */
static int ws_cmp_order(void *ctx1, void *ctx2) {
    struct job_context *job1 = (struct job_context *)ctx1;
    struct job_context *job2 = (struct job_context *)ctx2;

    if (job1->seqno < job2->seqno) return -1;
    if (job1->seqno > job2->seqno) return 1;
    return 0;
}

static void *galera_wsdb_configurator (
    enum wsdb_conf_param_id id, enum wsdb_conf_param_type type
) {
    switch (id) {
    case WSDB_CONF_LOCAL_CACHE_SIZE:
        return &galera_opts.local_cache_size;
    case WSDB_CONF_WS_PERSISTENCY:
        return &galera_opts.persistent_writesets;
    case WSDB_CONF_MARK_COMMIT_EARLY: // not supported any more?
    default:
        return NULL;
    }
}

#define GALERA_UPDATE_LAST_APPLIED(seqno)                              \
    assert (status.last_applied <= seqno);                             \
    status.last_applied = seqno;                                       \
    assert (status.last_applied <= last_recved);

static gcs_conn_t* galera_init_gcs (wsrep_t*      gh,
                                    const char*   node_name,
                                    const char*   node_incoming,
                                    gcs_seqno_t   last_recved,
                                    const uint8_t uuid[GCS_UUID_LEN])
{
    gcs_conn_t* ret;
    long rcode;

    GU_DBUG_ENTER("galera_init_gcs");

    ret = gcs_create(node_name, node_incoming);
    if (!ret) {
        gu_error ("Failed to create GCS conection handle");
        GU_DBUG_RETURN(NULL);
    }

    rcode = gcs_init (ret, last_recved, status.state_uuid.data);
    if (rcode) {
        gu_error ("Failed to initialize GCS state: %d (%s)",
                  rcode, strerror (-rcode));
        gcs_destroy (ret);
        GU_DBUG_RETURN(NULL);            
    }

    GU_DBUG_RETURN(ret);
}

static enum wsrep_status mm_galera_init(wsrep_t* gh,
                                        const struct wsrep_init_args* args)
{
    galera_state_t saved_state;
    int rcode = WSREP_OK;

    GU_DBUG_ENTER("galera_init");

    data_dir = strdup (args->data_dir);
    if (!data_dir) return WSREP_FATAL;

    /* Set up logging */
    gu_conf_set_log_callback((gu_log_cb_t)args->logger_cb);

    /* Set up options if any */
    if (args->options) galera_options_from_string (&galera_opts, args->options);

    wsdb_set_conf_param_cb(galera_wsdb_configurator);

    /* Set up initial state: */

    /* 1. use passed state arguments */
    status.state_uuid   = *(gu_uuid_t*)args->state_uuid;
    status.last_applied = args->state_seqno;

    /* 2. if nothing passed, try to read saved state from file */
    if (status.last_applied == WSREP_SEQNO_UNDEFINED &&
        !memcmp (&WSREP_UUID_UNDEFINED, &status.state_uuid, sizeof(gu_uuid_t))){

        rcode = galera_restore_state(data_dir, &saved_state);

        if (rcode) {
            gu_warn("GALERA state restore failed");
        } else {
            status.state_uuid   = saved_state.uuid;
            status.last_applied = saved_state.last_applied_seqno;
        }

        gu_info("Found stored state: " GU_UUID_FORMAT ":%lli",
                GU_UUID_ARGS(&status.state_uuid), status.last_applied);
    }

    gu_info("Configured state:   " GU_UUID_FORMAT ":%lli",
            GU_UUID_ARGS(&status.state_uuid), status.last_applied);

    /* 4. create and initialize GCS handle */
    gcs_conn = galera_init_gcs (gh, args->node_name, args->node_incoming,
                                status.last_applied, status.state_uuid.data);
    if (!gcs_conn) {
        gu_error ("Failed to initialize GCS state: %d (%s)",
                  rcode, strerror (-rcode));
        GU_DBUG_RETURN(WSREP_NODE_FAIL);            
    }

    last_recved = status.last_applied;

    my_idx = 0;

    /* initialize wsdb */
    wsdb_init(data_dir, (gu_log_cb_t)args->logger_cb);

    /* set the rest of callbacks */
    bf_apply_cb       = args->bf_apply_cb;
    ws_start_cb       = args->ws_start_cb;
    view_handler_cb   = args->view_handler_cb;
    sst_prepare_cb    = args->sst_prepare_cb;
    sst_donate_cb     = args->sst_donate_cb;

    /* initialize total order queue */
    cert_queue = gu_to_create(16384, GCS_SEQNO_FIRST);

    /* initialize commit queue */
    commit_queue = gu_to_create(16384, GCS_SEQNO_FIRST);

    gu_mutex_init(&commit_mtx, NULL);
    gu_mutex_init(&sst_mtx,    NULL);
    gu_cond_init (&sst_cond,   NULL);
    gu_mutex_init(&close_mtx,  NULL);
    gu_cond_init (&close_cond, NULL);

    conn_state = GALERA_INITIALIZED;

    /* create worker queue */
    applier_queue = job_queue_create(16, ws_conflict_check, ws_cmp_order);

#ifdef EXTRA_DEBUG
    /* debug level printing to /tmp directory */
    {
        DIR *dir = opendir("/tmp/galera");
        if (!dir) {
            mkdir("/tmp/galera", S_IRWXU | S_IRWXG);
        }
        wslog_L = fopen("/tmp/galera/ws_local.log", "w");
        wslog_G = fopen("/tmp/galera/ws_global.log", "w");
    }
#endif
    GU_DBUG_RETURN(WSREP_OK);
}

static enum wsrep_status
mm_galera_options_set (wsrep_t* gh, const char* opts_str)
{
    return galera_options_from_string (&galera_opts, opts_str);
}

static char*
mm_galera_options_get (wsrep_t* gh)
{
    return galera_options_to_string (&galera_opts);
}

static enum wsrep_status mm_galera_connect (wsrep_t *gh,
                                            const char* cluster_name,
                                            const char* cluster_url,
                                            const char* state_donor)
{
    int rcode;

    GU_DBUG_ENTER("galera_connect");

    // save donor name for using in configuration handler
    if (sst_donor) free ((char*)sst_donor);
    sst_donor = strdup (state_donor);
    if (!sst_donor) GU_DBUG_RETURN(WSREP_FATAL);

    rcode = gcs_open(gcs_conn, cluster_name, cluster_url);
    if (rcode) {
	gu_error("gcs_open(%p, %s, %s) failed: %d (%s)",
                 &gcs_conn, cluster_name, cluster_url,
                 rcode, strerror(-rcode));
	GU_DBUG_RETURN(WSREP_NODE_FAIL);
    }

    gu_info("Successfully opened GCS connection to %s", cluster_name);

    status.stage = GALERA_STAGE_JOINING;
    conn_state   = GALERA_CONNECTED;

    GU_DBUG_RETURN(rcode);
}

static enum wsrep_status mm_galera_disconnect(wsrep_t *gh,
                                              wsrep_uuid_t*  app_uuid,
                                              wsrep_seqno_t* app_seqno)
{
    int rcode;

    GU_DBUG_ENTER("galera_disconnect");

    if (!gcs_conn) {
        conn_state   = GALERA_INITIALIZED;
        GU_DBUG_RETURN (WSREP_NODE_FAIL); //shouldn't we just ignore it?
    }

    if (GALERA_CONNECTED == conn_state) {

        conn_state   = GALERA_INITIALIZED;
        status.stage = GALERA_STAGE_INIT;

        rcode = gcs_close(gcs_conn);
        if (rcode) {
            gu_error ("Failed to close GCS connection handle: %d (%s)",
                      rcode, strerror(-rcode));
            GU_DBUG_RETURN(WSREP_NODE_FAIL);
        }

        gu_info("Closed GCS connection");
    }

    // synchronize with self-leave
    gu_mutex_lock (&close_mtx);
#if SELF_LEAVE // currently vsbes does not deliver self-leave message
    while (-1 != my_idx /* self-leave */) gu_cond_wait (&close_cond,&close_mtx);
#else
    sleep (5);
#endif
    gu_mutex_unlock (&close_mtx);

    if (app_uuid)  *app_uuid  = *(wsrep_uuid_t*)&status.state_uuid;
    if (app_seqno) *app_seqno = status.last_applied;

    GU_DBUG_RETURN(WSREP_OK);
}

static void mm_galera_tear_down(wsrep_t *gh)
{
    int rcode;
    galera_state_t saved_state;

    switch (conn_state) {

    case GALERA_CONNECTED:
        mm_galera_disconnect (gh, NULL, NULL);

    case GALERA_INITIALIZED:
        if (gcs_conn)     gcs_destroy (gcs_conn);
        if (cert_queue)   gu_to_destroy(&cert_queue);
        if (commit_queue) gu_to_destroy(&commit_queue);

        wsdb_close();

        /* store persistent state*/
        memcpy(saved_state.uuid.data, status.state_uuid.data, GU_UUID_LEN);
        saved_state.last_applied_seqno = status.last_applied;

        rcode = galera_store_state(data_dir, &saved_state);
        if (rcode) {
            gu_error("GALERA state store failed: %d", rcode);
            gu_info(" State: "GU_UUID_FORMAT":%lli",
                    GU_UUID_ARGS(&status.state_uuid), status.last_applied);
        }

        if (data_dir)  free ((char*)data_dir);
        if (sst_donor) free ((char*)sst_donor);

    case GALERA_UNINITIALIZED:
        break;

    default:
        gu_fatal ("Unrecognized Galera state on teardown: %d", conn_state);
        assert (0);
        abort();
    }

    return;
}

#ifdef EXTRA_DEBUG
static void print_ws(FILE* fid, struct wsdb_write_set *ws, gcs_seqno_t seqno) {
    u_int16_t i;

    if (!fid) return;

    fprintf(fid, "WS: %llu\n", (long long unsigned int)seqno);
    for (i=0; i < ws->query_count; i++) {
      char *query = gu_malloc (ws->queries[i].query_len + 1);
      memset(query, '\0', ws->queries[i].query_len + 1);
      memcpy(query, ws->queries[i].query, ws->queries[i].query_len);
      fprintf(fid, "QUERY (%llu): %s\n", (long long unsigned int)seqno, query);
      gu_free (query);
    }
    fflush(fid);
}

#define PRINT_WS(fid, ws, seqno) { print_ws(fid, ws, seqno); }
#else
#define PRINT_WS(fid, ws, seqno)
#endif // EXTRA_DEBUG

static wsrep_status_t apply_queries(void *app_ctx, struct wsdb_write_set *ws) {
    u_int16_t i;

    GU_DBUG_ENTER(__PRETTY_FUNCTION__);

    wsrep_apply_data_t data;
    data.type = WSREP_APPLY_SQL;

    if (bf_apply_cb == NULL) {
        gu_error("data applier has not been defined"); 
        GU_DBUG_RETURN(WSREP_FATAL);
    }

    /* SQL statement apply method */
    for (i=0; i < ws->query_count; i++) {
        data.u.sql.stm      = ws->queries[i].query;
        data.u.sql.len      = ws->queries[i].query_len;
        data.u.sql.timeval  = ws->queries[i].timeval;
        data.u.sql.randseed = ws->queries[i].randseed;

        switch (bf_apply_cb(app_ctx, &data)) {
        case WSREP_OK: break;
        case WSREP_NOT_IMPLEMENTED: break;
        case WSREP_FATAL:
        default: 
            {
                char *query = gu_malloc (ws->queries[i].query_len + 1);
                memset(query, '\0',(ws->queries[i].query_len + 1));
                memcpy(query, ws->queries[i].query, ws->queries[i].query_len);
                gu_error("query apply failed: %s", query);
                gu_free (query);
                GU_DBUG_RETURN(WSREP_TRX_FAIL);
                break;
            }
        }
    }
    GU_DBUG_RETURN(WSREP_OK);
}

static wsrep_status_t apply_rows(void *app_ctx, struct wsdb_write_set *ws) {
    u_int16_t i;
    wsrep_apply_data_t data;
    GU_DBUG_ENTER("apply_rows");
    data.type = WSREP_APPLY_ROW;

    /* row data apply method */
    for (i=0; i < ws->item_count; i++) {
        wsrep_status_t rcode;
        if (ws->items[i].data_mode != ROW) {
            gu_error("bad row mode: %d for item: %d", 
		     ws->items[i].data_mode, i);
            continue;
        }

        data.u.row.buffer = ws->items[i].u.row.data;
        data.u.row.len = ws->items[i].u.row.length;

        switch ((rcode = bf_apply_cb(app_ctx, &data))) {
        case WSREP_OK: break;
        case WSREP_NOT_IMPLEMENTED: break;
        case WSREP_FATAL:
        default:
            {
                gu_warn("row apply failed: %d", rcode);
                GU_DBUG_RETURN(rcode);
                break;
            }
        }
    }
    GU_DBUG_RETURN(WSREP_OK);
}

static wsrep_status_t apply_write_set(
    void *app_ctx, struct wsdb_write_set *ws
) {
    u_int16_t i;
    wsrep_status_t rcode = WSREP_OK;

    GU_DBUG_ENTER("apply_write_set");
    if (bf_apply_cb == NULL) {
        gu_error("data applier has not been defined"); 
        GU_DBUG_RETURN(WSREP_FATAL);
    }

    /* first, applying connection context statements */
    if (ws->level == WSDB_WS_QUERY) {
        wsrep_apply_data_t data;
        data.type = WSREP_APPLY_SQL;
        data.u.sql.timeval  = (time_t)0;
        data.u.sql.randseed = 0;

        for (i=0; i < ws->conn_query_count; i++) {
            data.u.sql.stm      = ws->conn_queries[i].query;
            data.u.sql.len      = ws->conn_queries[i].query_len;

            switch (bf_apply_cb(app_ctx, &data)) {
            case WSREP_OK: break;
            case WSREP_NOT_IMPLEMENTED: break;
            case WSREP_FATAL: 
            default:
                {
                    char *query = gu_malloc (ws->conn_queries[i].query_len + 1);
                    memset(query, '\0', ws->conn_queries[i].query_len + 1);
                    memcpy(query, ws->conn_queries[i].query, 
                           ws->conn_queries[i].query_len);
                    gu_error("connection query apply failed: %s", query);
                    gu_free (query);
                    GU_DBUG_RETURN(WSREP_TRX_FAIL);
                    break;
                }
            }
        }
    }
    switch (ws->level) {
    case WSDB_WS_QUERY:     
        rcode = apply_queries(app_ctx, ws);
        break;
    case WSDB_WS_DATA_ROW:
        // TODO: implement
        rcode = apply_rows(app_ctx, ws);
        break;
    case WSDB_WS_DATA_RBR: 
        {
            wsrep_apply_data_t data;
            data.type = WSREP_APPLY_APP;
            data.u.app.buffer = (uint8_t *)ws->rbr_buf;
            data.u.app.len = ws->rbr_buf_len;

            rcode = bf_apply_cb(app_ctx, &data);
            break;
        }
    case WSDB_WS_DATA_COLS: 
        gu_error("column data replication is not supported yet");
        GU_DBUG_RETURN(WSREP_TRX_FAIL);
    default:
        assert(0);
        break;
    }

    switch (rcode) {
    case WSREP_OK: break;
    case WSREP_NOT_IMPLEMENTED: break;
    case WSREP_FATAL:
    default:
        gu_error("apply failed for: %d, in level: %d", rcode, ws->level);
        GU_DBUG_RETURN(WSREP_FATAL);
    }
    GU_DBUG_RETURN(WSREP_OK);
}

static wsrep_status_t apply_query(void *app_ctx, char *query, int len) {

    int rcode;
    wsrep_apply_data_t data;

    GU_DBUG_ENTER("apply_commit");

    if (bf_apply_cb == NULL) {
        gu_error("data applier has not been defined"); 
        GU_DBUG_RETURN(WSREP_FATAL);
    }

    data.type           = WSREP_APPLY_SQL;
    data.u.sql.stm      = query;
    data.u.sql.len      = len;
    data.u.sql.timeval  = (time_t)0;
    data.u.sql.randseed = 0;
    
    rcode = bf_apply_cb(app_ctx, &data);
    if (rcode != WSREP_OK) {
        gu_error("query commit failed: %d query '%s'", rcode, query);
        GU_DBUG_RETURN(WSREP_TRX_FAIL);
    }
    
    GU_DBUG_RETURN(WSREP_OK);
}

static ulong const    report_interval = 200;
static volatile ulong report_counter  = 0;

// fast funciton to be run inside commit_queue critical section
static inline bool report_check_counter ()
{
    return (++report_counter > report_interval && !(report_counter = 0));
}

// this should be run after commit_queue is released
static inline void report_last_committed (
    gcs_conn_t* gcs_conn
) {
    gcs_seqno_t safe_seqno = wsdb_get_safe_to_discard_seqno();
    long ret;

    gu_debug ("Reporting: safe to discard: %lld, last applied: %lld, "
              "last received: %lld",
              safe_seqno, status.last_applied, last_recved);

    if ((ret = gcs_set_last_applied(gcs_conn, safe_seqno))) {
        gu_warn ("Failed to report last committed %llu, %d (%s)",
                 safe_seqno, ret, strerror (-ret));
        // failure, set counter to trigger new attempt
        report_counter += report_interval;
    }
}

static inline void truncate_trx_history (gcs_seqno_t seqno)
{
    static long const truncate_interval = 100;
    static gcs_seqno_t last_truncated = 0;

    if (last_truncated + truncate_interval < seqno) {
        gu_debug ("Purging history up to %llu", seqno);
        wsdb_purge_trxs_upto(seqno);
        last_truncated = seqno;
        gu_debug ("Purging done to %llu", seqno);
    }
}

// returns true if action is to be applied and false if to be skipped
// should always be called while holding cert_queue
static inline bool
galera_update_global_seqno (gcs_seqno_t seqno)
{
    // Seems like we cannot enforce sanity check here - some replicated
    // writesets get cancelled and never make it to this point (TO monitor).
    // Hence holes in global seqno are inevitable here.
    if (gu_likely (last_recved < seqno)) {
        last_recved = seqno;
        return true;
    }
    else {
        return false;
    }
}

static wsrep_status_t process_conn_write_set( 
    struct job_worker *applier, void *app_ctx, struct wsdb_write_set *ws, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    bool do_report;
    wsrep_status_t rcode = WSREP_OK;

    /* wait for total order */
    GALERA_GRAB_QUEUE (cert_queue,   seqno_l);
    GALERA_GRAB_QUEUE (commit_queue, seqno_l);

    if (gu_likely(galera_update_global_seqno(seqno_g))) {
        /* Global seqno ok, certification ok (not needed?) */
        rcode = apply_write_set(app_ctx, ws);
        if (rcode) {
            gu_error ("unknown galera fail: %d trx: %llu", rcode, seqno_l);
        }
    }
    
    /* release total order */
    GALERA_RELEASE_QUEUE (cert_queue, seqno_l);
    GALERA_UPDATE_LAST_APPLIED (seqno_g);
    do_report = report_check_counter();
    GALERA_RELEASE_QUEUE (commit_queue, seqno_l);

    wsdb_set_global_trx_committed(seqno_g);

    if (do_report) report_last_committed(gcs_conn);
    
    return rcode;
}

enum wsrep_status process_query_write_set_applying(
    struct job_worker *applier, void *app_ctx, struct wsdb_write_set *ws, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    struct job_context ctx;

    enum wsrep_status  rcode = WSREP_OK;
    bool do_report           = false;
    int  attempts            = 0;

#define MAX_APPLY_ATTEMPTS 10 // try applying 10 times

    /* synchronize with other appliers */
    ctx.seqno = seqno_l;
    ctx.ws    = ws;
    /* why this? slave applier can process either ws transactions or TO
     * isolation queries. local connections, which are promoted as appliers
     * are either of type: TO_ISOLATION or REPLAYING
     */
    ctx.type  = (applier->type == JOB_SLAVE) ?
        ((ws->type == WSDB_WS_TYPE_CONN) ? JOB_TO_ISOLATION : JOB_SLAVE) :
      applier->type;

    job_queue_start_job(applier_queue, applier, (void *)&ctx);

 retry:
    while((rcode = apply_write_set(app_ctx, ws))) {
        if (attempts == 0) 
            gu_warn("ws apply failed, rcode: %d, seqno: %llu, last_seen: %llu", 
                    rcode, seqno_g, ws->last_seen_trx
        );

        if (apply_query(app_ctx, "rollback\0", 9)) {
            gu_warn("ws apply rollback failed, seqno: %llu, last_seen: %llu", 
                    seqno_g, ws->last_seen_trx);
        }

#ifdef EXTRA_DEBUG
        /* print conflicting rbr buffer for later analysis */
        if (attempts == 1) {
            char file[256];
            memset(file, '\0', 256);
            sprintf(file, "/tmp/galera/ws_%llu.bin", seqno_l);
            FILE *fd= fopen(file, "w");
            fwrite(ws->rbr_buf, 1, ws->rbr_buf_len, fd);
            fclose(fd); 
        }
#endif
        ++attempts;

        /* avoid retrying if fatal error happened */
        if (rcode == WSREP_FATAL) attempts = MAX_APPLY_ATTEMPTS;

        /* break if retry limit has been reached */
        if (attempts == MAX_APPLY_ATTEMPTS) break;

    }
    if (attempts == MAX_APPLY_ATTEMPTS) {
        gu_warn("ws applying is not possible, %lld - %lld", seqno_g, seqno_l);
        job_queue_end_job(applier_queue, applier);
        return WSREP_TRX_FAIL;
    }

    /* 
     * Grab commit queue for commit time
     * (can't use here GALERA_GRAB_QUEUE(seqno_l) macro)
     */
    switch (applier->type) {
    case JOB_SLAVE:
        rcode = while_eagain(gu_to_grab, commit_queue, seqno_l);
        break;
    case JOB_REPLAYING:
    case JOB_TO_ISOLATION:
        /* replaying jobs have reserved commit_queue in the beginning */
        rcode = 0;
        break;
    }
    switch (rcode) {
    case 0: break;
    case -ECANCELED:
        gu_debug("BF canceled in commit queue for %lld %lld", seqno_g, seqno_l);
        // falling through

    case -EINTR:
        gu_debug("BF interrupted in commit queue for %llu", seqno_l);

        /*
         * here we have logical problem. We cannot tell, if interrupt
         * was meant for us, or if we already aborted, and retried earlier
         * in applying phase. We decide to abort once more, just for safety
         * to avoid hanging.
         * This needs a better implementation, to identify if interrupt
         * is real or not.
         */
        if (attempts > 0) {
            attempts = 0;
            //goto retry_commit;
            gu_info("BF interrupted (retries>0) in commit queue for %lld", 
                    seqno_l
            );
        }
        if (apply_query(app_ctx, "rollback\0", 9)) {
            gu_warn("ws apply rollback failed for: %lld %lld, last_seen: %lld", 
                    seqno_g, seqno_l, ws->last_seen_trx
            );
        }
        goto retry;
        break;

    default:
        gu_fatal("BF commit queue grab failed (%lld %lld)", seqno_g, seqno_l);
        abort();
    }

    gu_debug("GALERA ws commit for: %lld %lld", seqno_g, seqno_l); 
    if (apply_query(app_ctx, "commit\0", 7)) {
        gu_warn("ws apply commit failed, seqno: %lld %lld, last_seen: %lld", 
                seqno_g, seqno_l, ws->last_seen_trx);
        goto retry;
    }
    job_queue_end_job(applier_queue, applier);

    /* 
     * if trx is replaying, we will call post_commit later on,
     * => slave applying "bookkeeping" must be avoided.
     * post_commit will also release commit_queue
     */
    if (applier->type == JOB_SLAVE) {
        GALERA_UPDATE_LAST_APPLIED (seqno_g);

        do_report = report_check_counter ();

        GALERA_RELEASE_QUEUE (commit_queue, seqno_l);

        wsdb_set_global_trx_committed(seqno_g);

        if (do_report) report_last_committed(gcs_conn);
    }
    return WSREP_OK;
}
/*
  similar to post gcs_repl part of `galera_commit' to apply remote WS
*/
static enum wsrep_status process_query_write_set( 
    struct job_worker *applier, void *app_ctx, struct wsdb_write_set *ws, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    int rcode;
    enum wsrep_status ret_code;

    /* wait for total order */
    GALERA_GRAB_QUEUE (cert_queue, seqno_l);

#ifdef GALERA_WORKAROUND_197
    rcode = wsdb_append_write_set(seqno_g, ws);
    if (gu_unlikely(!galera_update_global_seqno(seqno_g))) {
        /* Outdated writeset, skip */
        rcode = WSDB_CERTIFICATION_SKIP;
    }
#else
    if (gu_likely(galera_update_global_seqno(seqno_g))) {
        /* Global seqno OK, do certification test */
        rcode = wsdb_append_write_set(seqno_g, ws);
    }
    else {
        /* Outdated writeset, skip */
        rcode = WSDB_CERTIFICATION_SKIP;
    }
#endif

    /* release total order */
    GALERA_RELEASE_QUEUE (cert_queue, seqno_l);

    PRINT_WS(wslog_G, ws, seqno_l);

    gu_debug("remote trx seqno: %llu %llu last_seen_trx: %llu %llu, cert: %d", 
             seqno_g, seqno_l, ws->last_seen_trx, last_recved, rcode);

    switch (rcode) {
    case WSDB_OK:   /* certification ok */
    {
        rcode = process_query_write_set_applying( 
            applier, app_ctx, ws, seqno_g, seqno_l
        );

        /* stop for any dbms error */
        if (rcode != WSDB_OK) {
            gu_fatal("could not apply trx: %lld %lld", seqno_g, seqno_l);
            return WSREP_FATAL;
        }
        ret_code = WSREP_OK;
        break;
    }
    case WSDB_CERTIFICATION_FAIL:
        /* certification failed, release */
        gu_debug("trx certification failed: (%lld %lld) last_seen: %lld",
                seqno_g, seqno_l, ws->last_seen_trx);

        PRINT_WS(wslog_G, ws, seqno_g);

        /* Cancel commit queue */
        if (applier->type == JOB_SLAVE) {
            GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
        } else {
            /* replaying job has grabbed commit queue in the beginning */
            GALERA_UPDATE_LAST_APPLIED (seqno_g);
            GALERA_RELEASE_QUEUE (commit_queue, seqno_l);
        }

        ret_code = WSREP_TRX_FAIL;
        break;
    case WSDB_CERTIFICATION_SKIP:
        /* Cancel commit queue */
        if (applier->type == JOB_SLAVE) {
            GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
        } else {
            /* replaying job has grabbed commit queue in the beginning */
            GALERA_UPDATE_LAST_APPLIED (seqno_g);
            GALERA_RELEASE_QUEUE (commit_queue, seqno_l);
        }
        ret_code = WSREP_OK;
        break;
    default:
        gu_error(
            "unknown galera fail: %d trdx: %lld %lld", rcode, seqno_g, seqno_l
        );
        ret_code = WSREP_FATAL;
        break;
    }

    return ret_code;
}

static wsrep_status_t process_write_set( 
    struct job_worker *applier, void *app_ctx, uint8_t *data, size_t data_len, 
    gcs_seqno_t seqno_g, gcs_seqno_t seqno_l
) {
    struct wsdb_write_set ws;
    XDR xdrs;
    wsrep_status_t rcode = WSREP_OK;

    xdrmem_create(&xdrs, (char *)data, data_len, XDR_DECODE);
    if (!xdr_wsdb_write_set(&xdrs, &ws)) {
        gu_fatal("GALERA XDR allocation failed, len: %d seqno: (%lld %lld)", 
                 data_len, seqno_g, seqno_l
        );
        abort();
    }

    /* key composition is not sent through xdr */
    if (ws.key_composition) {
        gu_warn("GALERA XDR encoding returned key comp, seqno: (%lld %lld)",
                seqno_g, seqno_l
        );
    }

    ws_start_cb(app_ctx, seqno_l);

    switch (ws.type) {
    case WSDB_WS_TYPE_TRX:
        rcode = process_query_write_set(applier, app_ctx, &ws, seqno_g,seqno_l);
        break;
    case WSDB_WS_TYPE_CONN:
        rcode = process_conn_write_set(applier, app_ctx, &ws, seqno_g, seqno_l);
        break;
    }

    ws_start_cb(app_ctx, 0);

    /* free xdr objects */
    xdrs.x_op = XDR_FREE;
    xdr_wsdb_write_set(&xdrs, &ws);

    return rcode;
}

/* To be called only in isolation in galera_handle_configuration() */
static void
galera_join()
{
    long ret;

    do {
        ret = gcs_join (gcs_conn, status.last_applied);
    }
    while ((-EAGAIN == ret) &&
           (usleep(GALERA_USLEEP_1_SECOND), true));

    if (ret) {
        galera_state_t st = { status.last_applied, status.state_uuid };

        galera_store_state (data_dir, &st);

        gu_fatal ("Could not send join message: "
                  "%d (%s). Aborting.", ret, strerror(-ret));
        abort(); // TODO: see #170, gcs_join() must be reworked
    }
    else {
        status.stage = GALERA_STAGE_JOINED;
    }
}

/*!
 * This is needed because ST is (and needs to be) _asynchronous_ with
 * replication. As a result, only application thread knows when and how ST
 * completed and if we need another one. See #163
 */
static void
galera_check_st_required (gcs_act_conf_t* conf)
{
    if (conf->st_required) {
        gu_uuid_t* conf_uuid = (gu_uuid_t*)conf->group_uuid;

        assert (conf->conf_id >= 0);

        if (!gu_uuid_compare (&status.state_uuid, conf_uuid)) {
            // same history
            if (GALERA_STAGE_JOINED == status.stage) {
                // if we took ST already, it may exceed conf->seqno
                // (ST is asynchronous!)
                conf->st_required = (status.last_applied < conf->seqno);
            }
            else {
                conf->st_required = (status.last_applied != conf->seqno);
            }
        }

        if (conf->st_required) {
            status.stage = GALERA_STAGE_JOINING;
        }
        else {
            // rewrote conf->st_required, tell GCS about it
            galera_join();
        }
    }
}

/*!
 * @return
 *        donor index (own index in case when no state transfer needed)
 *        or negative error code (-1 if configuration in non-primary)
 */
static long
galera_handle_configuration (wsrep_t* gh,
                             const gcs_act_conf_t* conf, gcs_seqno_t conf_seqno)
{
    long ret = 0;
    gu_uuid_t* conf_uuid = (gu_uuid_t*)conf->group_uuid;

    GU_DBUG_ENTER("galera_handle_configuration");

    gu_info ("New %s configuration: %lld, "
             "seqno: %lld, group UUID: "GU_UUID_FORMAT
             ", members: %zu, my idx: %zd",
             conf->conf_id > 0 ? "PRIMARY" : "NON-PRIMARY",
             (long long)conf->conf_id, (long long)conf->seqno,
             GU_UUID_ARGS(conf_uuid), conf->memb_num, conf->my_idx);

    my_idx = conf->my_idx; // this is always true.

    GALERA_GRAB_QUEUE (cert_queue,   conf_seqno);
    GALERA_GRAB_QUEUE (commit_queue, conf_seqno);

    galera_check_st_required ((gcs_act_conf_t*)conf);

#ifdef GALERA_WORKAROUND_197
    if (conf->seqno >= 0) wsdb_purge_trxs_upto(conf->seqno);
    wsdb_set_global_trx_committed(conf->seqno);
#endif

    view_handler_cb (galera_view_info_create (conf));

    if (conf->conf_id >= 0) {                // PRIMARY configuration

        if (conf->st_required)
        {
            void*   app_req = NULL;
            ssize_t app_req_len;
            int const retry_sec = 1;

            // GCS determined that we need to request state transfer.
            gu_info ("State transfer required:"
                     "\n\tGroup state: "GU_UUID_FORMAT":%lld"
                     "\n\tLocal state: "GU_UUID_FORMAT":%lld",
                     GU_UUID_ARGS(conf_uuid), conf->seqno,
                     GU_UUID_ARGS(&status.state_uuid), status.last_applied);

            /* Starting state snapshot transfer */
            gu_mutex_lock (&sst_mtx);

            status.stage = GALERA_STAGE_SST_PREPARE;
            app_req_len = sst_prepare_cb (&app_req);

            if (app_req_len < 0) {

                status.stage = GALERA_STAGE_JOINING;
                ret = app_req_len;
                gu_error ("Application failed to prepare for receiving "
                          "state transfer: %d (%s)", ret, strerror(-ret));
            }
            else do {
                // Actually this loop can be done even in this thread:
                // until we succeed in sending state transfer request, there's
                // nothing else for us to do.
                gcs_seqno_t seqno_l;

                assert (app_req);

                if (galera_invalidate_state (data_dir)) abort();

                status.stage = GALERA_STAGE_RST_SENT;
                ret = gcs_request_state_transfer (gcs_conn,
                                                  app_req, app_req_len,
                                                  sst_donor, &seqno_l);
                if (ret < 0) {
                    if (ret != -EAGAIN) {
                        galera_state_t st =
                            { status.last_applied, status.state_uuid };

                        status.stage = GALERA_STAGE_RST_FAILED;
                        gu_error ("Requesting state snapshot transfer failed: "
                                  "%d (%s)", ret, strerror(-ret));
                        // try not to lose state information if RST fails
                        galera_store_state (data_dir, &st);
                    }
                    else {
                        gu_info ("Requesting state snapshot transfer failed: "
                                 "%d (%s). Retrying in %d seconds",
                                 ret, strerror(-ret), retry_sec);
                    }
                }

                if (seqno_l > GCS_SEQNO_NIL) {

                    GALERA_SELF_CANCEL_QUEUE (cert_queue,   seqno_l);
                    GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
                }

            } while ((ret == -EAGAIN) &&
                     (usleep(retry_sec * GALERA_USLEEP_1_SECOND), true));

            if (app_req) free (app_req);

            if (ret >= 0) {

                status.stage = GALERA_STAGE_SST_WAIT;
                gu_info ("Requesting state transfer: success, donor %ld", ret);
                assert (ret != my_idx);

                /* Here wait for application to call galera_state_received(),
                 * which will set my_uuid and my_seqno */
                gu_cond_wait (&sst_cond, &sst_mtx);

                if (gu_uuid_compare (&sst_uuid, conf_uuid) ||
                    sst_seqno < conf->seqno)
                {
                    status.stage = GALERA_STAGE_SST_FAILED;
                    gu_fatal ("Application received wrong state:"
                              "\n\tReceived: "GU_UUID_FORMAT" :    %lld"
                              "\n\tRequired: "GU_UUID_FORMAT" : >= %lld",
                              GU_UUID_ARGS(&sst_uuid), sst_seqno,
                              GU_UUID_ARGS(conf_uuid), conf->seqno);
                    assert(0);
                    abort(); // just abort for now. Ideally reconnect to group.
                }
                else {
                    status.state_uuid   = sst_uuid;
                    status.last_applied = sst_seqno;
                    last_recved = status.last_applied;

                    gu_info ("Application state transfer complete: "
                             GU_UUID_FORMAT":%lld",
                             GU_UUID_ARGS(&status.state_uuid),
                             status.last_applied);

                    galera_join();
                }
            }
            else {
                // Until we develop a clean way out of this situation: return
                // from this function means unlocking queues and letting
                // slave threads to apply actions over unsynchronized state.
                gu_fatal ("Aborting to prevent state corruption.");
                assert(0);
                abort();
            }

            gu_mutex_unlock (&sst_mtx);
        }
        else {                              /* no state transfer required */
            if (1 == conf->conf_id) {

                status.state_uuid   = *conf_uuid;
                status.last_applied = conf->seqno;

                last_recved = status.last_applied;
            }
            else {
                /* Here we can't test for seqno equality,
                 * there can be gaps because of BFs and stuff */
                if (/*last_recved < conf->seqno || */
                    gu_uuid_compare (&status.state_uuid, conf_uuid)) {

                    gu_fatal ("Internal replication error: no state transfer "
                              "requested while this node state is not "
                              "the same as the group."
                              "\n\tGroup: "GU_UUID_FORMAT":%lld"
                              "\n\tNode:  "GU_UUID_FORMAT":%lld",
                              GU_UUID_ARGS(conf_uuid),          conf->seqno,
                              GU_UUID_ARGS(&status.state_uuid), last_recved);
                    assert(0);
                    abort(); // just abort for now. Ideally reconnect to group.
                }

                // workaround for #182, should be safe since we're in isolation
                last_recved = conf->seqno;
            }

            if (galera_invalidate_state (data_dir)) abort();

            status.stage = GALERA_STAGE_JOINED;
            ret = my_idx;
        }
    }
    else {
        // NON PRIMARY configuraiton
        if (status.last_applied >= 0 &&
            gu_uuid_compare (&status.state_uuid, &GU_UUID_NIL)) {
            // try not to lose state information
            galera_state_t st = { status.last_applied, status.state_uuid };
            galera_store_state (data_dir, &st);
        }
        else {
            assert (status.last_applied < 0 &&
                    !gu_uuid_compare (&status.state_uuid, &GU_UUID_NIL));
        }

        status.stage = GALERA_STAGE_JOINING;

        ret = -1;

#if SELF_LEAVE
        gu_mutex_lock (&close_mtx);
        if (-1 == my_idx /* self-leave */) gu_cond_signal (&close_cond);
        gu_mutex_unlock (&close_mtx);
#endif
    }

    GALERA_RELEASE_QUEUE (cert_queue,   conf_seqno);
    GALERA_RELEASE_QUEUE (commit_queue, conf_seqno);

    GU_DBUG_RETURN(ret);
}

static enum wsrep_status mm_galera_recv(wsrep_t *gh, void *app_ctx) {
    int rcode;
    struct job_worker *applier;
    bool shutdown = false;
    enum wsrep_status ret_code;

    /* we must have gcs connection */
    if (!gcs_conn) {
        gu_info("recv method cannot start, no gcs connection");
        return WSREP_NODE_FAIL;
    }

    applier = job_queue_new_worker(applier_queue, JOB_SLAVE);

    if (!applier) {
        gu_error("galera, could not create applier");
        gu_info("active_workers: %d, max_workers: %d",
            applier_queue->active_workers, applier_queue->max_concurrent_workers
        );
        return WSREP_NODE_FAIL;
    }

    while (!shutdown) {
        gcs_act_type_t  action_type;
        size_t          action_size;
        void*           action;
        gcs_seqno_t     seqno_g, seqno_l;

        rcode = gcs_recv(
            gcs_conn, &action, &action_size, &action_type, &seqno_g, &seqno_l
        );

#ifdef EXTRA_DEBUG
        gu_info ("gcs_recv(): act_type: %u, act_size: %u, act_id: %lld, "
                "local: %lld, rcode: %d (%s)",
                 action_type, action_size, seqno_g, seqno_l, rcode,
                 strerror(-rcode));
#endif
	if (rcode <= 0) {
            /* must return immediately, seqnos are not usable */
            gu_info ("gcs_recv() returned %d (%s)", rcode, strerror(-rcode));
            return WSREP_CONN_FAIL;
        }

        assert (GCS_SEQNO_ILL != seqno_l);

        gu_debug("worker: %d with seqno: (%lld - %lld) type: %s recvd", 
                 applier->id, seqno_g, seqno_l, galera_act_type_str[action_type]
        );

        switch (action_type) {
        case GCS_ACT_TORDERED:
        {
            assert (GCS_SEQNO_ILL != seqno_g);

            status.received++;
            status.received_bytes += action_size;

            ret_code = process_write_set(
                applier, app_ctx, action, action_size, seqno_g, seqno_l
            );

            /* catch node failure */
            if (ret_code == WSREP_FATAL || ret_code == WSREP_NODE_FAIL) {
              shutdown = true;
            } 
            break;
        }
        case GCS_ACT_COMMIT_CUT:
            // synchronize
            GALERA_GRAB_QUEUE (cert_queue, seqno_l);
            truncate_trx_history (*(gcs_seqno_t*)action);
            GALERA_RELEASE_QUEUE (cert_queue, seqno_l);

            // Let other transaction continue to commit
            GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
            break;
        case GCS_ACT_CONF:
        {
            galera_handle_configuration (gh, action, seqno_l);
            break;
        }
        case GCS_ACT_STATE_REQ:
            if (0 <= seqno_l) { // should it be an assert?
                gu_info ("Got state transfer request.");

                // synchronize with app.
                GALERA_GRAB_QUEUE (cert_queue,   seqno_l);
                GALERA_GRAB_QUEUE (commit_queue, seqno_l);

                status.stage = GALERA_STAGE_DONOR;

                sst_donate_cb (action,
                               action_size,
                               (wsrep_uuid_t*)&status.state_uuid,
                               /* status.last_applied, see #182 */ last_recved);

                GALERA_RELEASE_QUEUE (cert_queue,   seqno_l);
                GALERA_RELEASE_QUEUE (commit_queue, seqno_l);
                /* To snap out of donor state application must call
                 * wsrep->sst_sent() when it is really done */
            }
            break;
        default:
            gu_error("bad gcs action value: %d, must abort", action_type);
            return WSREP_FATAL;
        }
        free (action); /* TODO: cache DATA actions at the end of commit queue
                        * processing. Therefore do not free them here. */
    }

    /* returning WSREP_NODE_FAIL or WSREP_FATAL */
    return ret_code;
}

static enum wsrep_status mm_galera_abort_pre_commit(wsrep_t *gh,
    const wsrep_seqno_t bf_seqno, const wsrep_trx_id_t victim_trx
) {
    enum wsrep_status ret_code = WSREP_OK;
    int rcode;
    wsdb_trx_info_t victim;

    /* 
     * this call must be allowed even if conn_state != GALERA_CONNECTED,
     * slave applier must be able to kill remaining conflicting trxs 
     */


    /* take commit mutex to be sure, committing trx does not
     * conflict with us
     */    
    gu_mutex_lock(&commit_mtx);

    wsdb_get_local_trx_info(victim_trx, &victim);
    
    gu_debug("abort_pre_commit trx state: %d seqno: %lld", 
         victim.state, victim.seqno_l);

    /* continue to kill the victim */
    switch (victim.state) {
    case WSDB_TRX_ABORTING_REPL:
    case WSDB_TRX_ABORTING_NONREPL:
    case WSDB_TRX_MUST_REPLAY:
    case WSDB_TRX_MUST_ABORT:
    case WSDB_TRX_MISSING:
        /* MUST_ABORT has been acknowledged or trx does not exist */
        break;

    case WSDB_TRX_VOID:
        ret_code = WSREP_WARNING;
        rcode = wsdb_assign_trx_state(victim_trx, WSDB_TRX_MUST_ABORT);
        if (rcode) {
            /* this is going to hang */
            gu_error("could not mark trx, aborting: trx %lld seqno: %lld", 
                     victim_trx, victim.seqno_l
            );
            //abort();
        } else {
            gu_debug("no seqno for trx, marked trx aborted: %llu", victim_trx);
        }
        break;

    case WSDB_TRX_REPLICATING:
        gu_debug("victim trx is replicating: %lld", victim.seqno_l);
        while (victim.state == WSDB_TRX_REPLICATING ) {
            gu_mutex_unlock(&commit_mtx);
            // @todo: This must be driven by signal.
            usleep (GALERA_USLEEP_FLOW_CONTROL);
            gu_mutex_lock(&commit_mtx);
            wsdb_get_local_trx_info(victim_trx, &victim);
        }

        if (victim.state != WSDB_TRX_REPLICATED)
	{
            assert(GCS_SEQNO_ILL == victim.seqno_l);
            gu_debug("victim trx didn't finish replicating");
            ret_code = WSREP_OK;
            break;
        }

        gu_debug("victim trx has replicated: %lld", victim.seqno_l);

        //falling through, we have valid seqno now

    default:
        if (victim.seqno_l < bf_seqno) {
            gu_debug("trying to interrupt earlier trx:  %lld - %lld", 
                     victim.seqno_l, bf_seqno);
            ret_code = WSREP_WARNING;
        } else {
            gu_debug("interrupting trx commit: trx_id %lld seqno %lld", 
                     victim_trx, victim.seqno_l);

            /* 
             * MUST_ABORT needs to be set only if TO interrupt does not work 
             */
            rcode = gu_to_interrupt(cert_queue, victim.seqno_l);
            switch (rcode) {
            case 0:
                ret_code = WSREP_OK;
                break;
            case -EAGAIN:
                /* 
                 * victim does not yet fit in TO queue, 
                 * slave queue has grown too long, we flag trx with
                 * must abort and assume he will abort himself
                 */
                gu_warn("victim trx does not fit cert TO queue");

                wsdb_assign_trx_state(victim_trx, WSDB_TRX_MUST_ABORT);
                ret_code = WSREP_OK;

                break;
            case -ERANGE:
                /* victim was canceled or used already */
                gu_debug("trx interupt fail in cert_queue: %d", rcode);
                ret_code = WSREP_OK;
                rcode = gu_to_interrupt(commit_queue, victim.seqno_l);
                if (rcode) {
                    wsdb_assign_trx_state(victim_trx, WSDB_TRX_MUST_ABORT);
                    gu_debug("trx interrupt fail in commit_queue: %d", rcode);
                    ret_code = WSREP_WARNING;
                }
            }
        }
    }
    gu_mutex_unlock(&commit_mtx);
    
    return ret_code;
}

static enum wsrep_status mm_galera_abort_slave_trx(
    wsrep_t *gh, wsrep_seqno_t bf_seqno, wsrep_seqno_t victim_seqno
) {
    enum wsrep_status ret_code = WSREP_OK;
    int rcode;

    /* take commit mutex to be sure, committing trx does not
     * conflict with us
     */
    
    gu_mutex_lock(&commit_mtx);

    if (victim_seqno < bf_seqno) {
        gu_debug("trying to interrupt earlier trx:  %lld - %lld", 
                 victim_seqno, bf_seqno);
        ret_code = WSREP_WARNING;
    } else {
        gu_debug("interrupting trx commit: seqno %lld", 
                 victim_seqno);

        rcode = gu_to_interrupt(cert_queue, victim_seqno);
        if (rcode) {
            gu_debug("BF trx interupt fail in cert_queue: %d", rcode);
            rcode = gu_to_interrupt(commit_queue, victim_seqno);
            if (rcode) {
                gu_warn("BF trx interrupt fail in commit_queue: %d", rcode);
                ret_code = WSREP_WARNING;
            }
        }
    }
    gu_mutex_unlock(&commit_mtx);

    return ret_code;
}

static enum wsrep_status mm_galera_post_commit(
    wsrep_t *gh, wsrep_trx_id_t trx_id
) {
    bool do_report = false;
    wsdb_trx_info_t trx;

    GU_DBUG_ENTER("galera_post_commit");

    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));

    gu_mutex_lock(&commit_mtx);
    wsdb_get_local_trx_info(trx_id, &trx);

    if (trx.state == WSDB_TRX_REPLICATED) {
        GALERA_UPDATE_LAST_APPLIED (trx.seqno_g);

        do_report = report_check_counter ();

        GALERA_RELEASE_QUEUE(commit_queue, trx.seqno_l);

        wsdb_delete_local_trx_info(trx_id);
    } else if (trx.state != WSDB_TRX_MISSING) {
        gu_debug("trx state: %d at galera_committed for: %lld", 
                 trx.state, trx.seqno_l
        );
    }

    status.local_commits++;

    gu_mutex_unlock(&commit_mtx);

    if (do_report) report_last_committed (gcs_conn);

    GU_DBUG_RETURN(WSREP_OK);
}

static enum wsrep_status mm_galera_post_rollback(
    wsrep_t *gh, wsrep_trx_id_t trx_id
) {
    wsdb_trx_info_t trx;

    GU_DBUG_ENTER("galera_post_rollback");

    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));

    gu_mutex_lock(&commit_mtx);

    wsdb_get_local_trx_info(trx_id, &trx);
    gu_debug("trx state: %d at post_rollback for: %lld", trx.state,trx.seqno_l);

    switch (trx.state) {
    case WSDB_TRX_MISSING: 
        break;
    case WSDB_TRX_MUST_REPLAY:
        /* we need trx info for replaying */
        break;
    case WSDB_TRX_VOID:
    case WSDB_TRX_ABORTING_NONREPL:
        /* voluntary rollback before replicating was attempted */
        wsdb_delete_local_trx(trx_id);
        wsdb_delete_local_trx_info(trx_id);
        break;
    case WSDB_TRX_MUST_ABORT:
    case WSDB_TRX_REPLICATED:
    case WSDB_TRX_ABORTING_REPL:
        /* these have replicated */
        if (gu_to_release(commit_queue, trx.seqno_l)) {
            gu_fatal("Could not release commit resource for %lld", trx.seqno_l);
            abort();
        }
        /* local trx was removed in pre_commit phase already */
        wsdb_delete_local_trx_info(trx_id);

        break;
    default:
        gu_debug("trx state: %d at galera_rolledback for: %lld", 
                 trx.state, trx.seqno_l
        );
        break;
    }

    gu_mutex_unlock(&commit_mtx);

    //gu_warn("GALERA rolledback, removed trx: %lu %llu", trx_id, seqno_l);
    GU_DBUG_RETURN(WSREP_OK);
}

static int check_certification_status_for_aborted(
    trx_seqno_t seqno_l, trx_seqno_t seqno_g, struct wsdb_write_set *ws
) {
    int rcode;
    /*
     * not sure if certification needs to wait for total order or not.
     * local trx has conflicted with some remote trx and we are interested 
     * to find out if this is a true conflict or dbms lock granularity issue.
     *
     * It would be safe to wait for all preceding trxs to certificate before
     * us. However, this is not simple to guarantee. There is a limited number 
     * of slave threads and each slave will eventually end up waiting for 
     * commit_queue. Therefore, all preceding remote trxs might not have passed
     * cert_queue and we would hang here waiting for cert_queue => deadlock.
     * 
     * for the time being, I just certificate here with all trxs which were
     * fast enough to certificate. This is wrong, and must be fixed.
     *
     * Maybe 'light weight' cert_queue would work here: We would just check that
     * seqno_l - 1 has certified and then do our certification.
     */

    rcode = wsdb_certification_test(ws, seqno_g, false);
    switch (rcode) {
    case WSDB_OK:
        gu_debug ("BF conflicting local trx has certified, "
                 "seqno: %llu %llu last_seen_trx: %llu", 
                 seqno_l, seqno_g, ws->last_seen_trx);
        /* certification ok */
        return WSREP_OK;

    case WSDB_CERTIFICATION_FAIL:
        /* certification failed, release */
        gu_debug("BF conflicting local trx certification fail: %llu - %llu",
                seqno_l, ws->last_seen_trx);

        PRINT_WS(wslog_L, ws, seqno_l);

        return WSREP_TRX_FAIL;

    default:  
        gu_fatal("wsdb append failed: seqno_g %llu seqno_l %llu",
                 seqno_g, seqno_l);
        abort();
        break;
    }
}


static enum wsrep_status mm_galera_pre_commit(
    wsrep_t *gh, wsrep_trx_id_t trx_id, wsrep_conn_id_t conn_id, 
    const char *rbr_data,size_t rbr_data_len
) {

    int                    rcode;
    struct wsdb_write_set *ws;
    XDR                    xdrs;
    int                    data_max;
    uint8_t                *data;
    int                    len;
    gcs_seqno_t            seqno_g, seqno_l;
    enum wsrep_status      retcode;
    wsdb_trx_info_t        trx;

    GU_DBUG_ENTER("galera_pre_commit");

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_CONN_FAIL;

    GU_DBUG_PRINT("galera", ("trx: %llu", trx_id));

    errno = 0;

#ifdef GALERA_USE_FLOW_CONTROL
    do {
#endif
    /* hold commit time mutex */
    gu_mutex_lock(&commit_mtx);
    /* check if trx was cancelled before we got here */
    wsdb_get_local_trx_info(trx_id, &trx);
    switch (trx.state) {
    case WSDB_TRX_MUST_ABORT:
	gu_debug("trx has been cancelled already: %llu", trx_id);

        /* trx can be removed from local cache, but trx info is still needed */
	if ((rcode = wsdb_delete_local_trx(trx_id))) {
	    gu_debug("could not delete trx: %llu", trx_id);
	}
        wsdb_assign_trx_state(trx_id, WSDB_TRX_ABORTING_NONREPL);
	gu_mutex_unlock(&commit_mtx);
	GU_DBUG_RETURN(WSREP_TRX_FAIL);
        break;
    case WSDB_TRX_MISSING:
	gu_debug("trx is missing from galera: %llu", trx_id);
	gu_mutex_unlock(&commit_mtx);
	GU_DBUG_RETURN(WSREP_TRX_MISSING);
        break;
    default:
      break;
    }
#ifdef GALERA_USE_FLOW_CONTROL
    /* what is happening here:
     * - first,  (gcs_wait() > 0) is evaluated, if not true, loop exits
     * - second, (++, unlock(), usleep(), true) is evaluated always to true,
     *   so we keep on looping.
     *   AFAIK usleep() is evaluated after unlock()
     */
    } while ((gcs_wait (gcs_conn) > 0) &&
             (status.fc_waits++, gu_mutex_unlock (&commit_mtx),
              usleep (GALERA_USLEEP_FLOW_CONTROL), true)
        );
#endif

    /* retrieve write set */
    ws = wsdb_get_write_set(trx_id, conn_id, rbr_data, rbr_data_len);
    if (!ws) {
        /* this is possibly autocommit query, need to let it continue */
        gu_mutex_unlock(&commit_mtx);
        gu_debug("empty local trx ws %llu", trx_id);
        GU_DBUG_RETURN(WSREP_OK);
    }

    /* ws can be removed from local cache already now */
    if ((rcode = wsdb_delete_local_trx(trx_id))) {
        gu_warn("could not delete trx: %llu", trx_id);
    }

    /* avoid sending empty write sets */
    if (ws->query_count == 0) {
        gu_warn("empty write set for: %llu", trx_id);
        GU_DBUG_RETURN(WSREP_OK);
    }

    /* encode with xdr */
    /* TODO: is not optimal to allocate data buffer for xdr encoding
     *       intermediate result.
     *       Should use xdrrec stream instead and encode directly on
     *       gcs channel as we go.
     */
    /* using xdr_sizeof to determine length of buffer. 
       For some reason this requires one additional byte at the end
    */
    data_max = xdr_sizeof((xdrproc_t)xdr_wsdb_write_set, (void *)ws) + 1;

    data = (uint8_t *)gu_malloc(data_max);
    memset(data, 0, data_max);
    xdrmem_create(&xdrs, (char *)data, data_max, XDR_ENCODE);
    if (!xdr_wsdb_write_set(&xdrs, ws)) {
        gu_error("xdr failed for: %llu", trx_id);
        gu_mutex_unlock(&commit_mtx);
        GU_DBUG_RETURN(WSREP_TRX_FAIL);
    }
    len = xdr_getpos(&xdrs);

    wsdb_assign_trx_state(trx_id, WSDB_TRX_REPLICATING);
    /* */
    gu_mutex_unlock(&commit_mtx);

    /* replicate through gcs */
    do {
        rcode = gcs_repl(gcs_conn, data, len, GCS_ACT_TORDERED,
                         &seqno_g, &seqno_l);
    } while (-EAGAIN == rcode && (usleep (GALERA_USLEEP_FLOW_CONTROL), true));

#ifdef EXTRA_DEBUG
    gu_debug ("gcs_repl(): size: %u, seqno_g: %llu, seqno_l: %llu, ret: %d",
             "GCS_ACT_TORDERED", len, seqno_g, seqno_l, rcode);
#endif
    if (rcode != len) {
        gu_error("gcs_repl() failed for: %llu, len: %d, rcode: %d (%s)",
                 trx_id, len, rcode, strerror (-rcode));
        assert (GCS_SEQNO_ILL == seqno_l);
        gu_mutex_lock(&commit_mtx);
        wsdb_assign_trx_seqno(
            trx_id, seqno_l, seqno_g, WSDB_TRX_ABORTING_NONREPL
        );
        gu_mutex_unlock(&commit_mtx);
        retcode = WSREP_CONN_FAIL;
        goto cleanup;
    }

    status.replicated++;
    status.replicated_bytes += len;

    assert (GCS_SEQNO_ILL != seqno_g);
    assert (GCS_SEQNO_ILL != seqno_l);

    gu_mutex_lock(&commit_mtx);

    /* record seqnos for local transaction */
    wsdb_assign_trx_seqno(trx_id, seqno_l, seqno_g, WSDB_TRX_REPLICATED);
    gu_mutex_unlock(&commit_mtx);

    if ((rcode = while_eagain_or_trx_abort(
        trx_id, gu_to_grab, cert_queue, seqno_l))
    ) {
        gu_debug("gu_to_grab aborted: %d seqno %llu", rcode, seqno_l);

        retcode = WSREP_TRX_FAIL;

        if (check_certification_status_for_aborted(
              seqno_l, seqno_g, ws) == WSREP_OK
        ) {
            wsdb_assign_trx_ws(trx_id, ws);
            wsdb_assign_trx_pos(trx_id, WSDB_TRX_POS_CERT_QUEUE);
            wsdb_assign_trx_state(trx_id, WSDB_TRX_MUST_REPLAY);
            GU_DBUG_RETURN(WSREP_BF_ABORT);
        } else {
            GALERA_SELF_CANCEL_QUEUE (cert_queue, seqno_l);
            GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
            wsdb_assign_trx_state(trx_id, WSDB_TRX_ABORTING_REPL);
       }
        goto cleanup;
    }

#ifdef GALERA_WORKAROUND_197
    rcode = wsdb_append_write_set(seqno_g, ws);
    if (gu_likely(galera_update_global_seqno (seqno_g))) {
#else
    if (gu_likely(galera_update_global_seqno (seqno_g))) {
        /* Global seqno OK, do certification test */
        rcode = wsdb_append_write_set(seqno_g, ws);
#endif
        switch (rcode) {
        case WSDB_OK:
            gu_debug ("local trx certified, "
                      "seqno: %lld %lld last_seen_trx: %lld", 
                      seqno_g, seqno_l, ws->last_seen_trx);
            /* certification ok */
            retcode = WSREP_OK;
            break;
        case WSDB_CERTIFICATION_FAIL:
            /* certification failed, release */
            retcode = WSREP_TRX_FAIL;

            gu_debug("local trx commit certification failed: %lld %lld - %lld",
                     seqno_g, seqno_l, ws->last_seen_trx);
            PRINT_WS(wslog_L, ws, seqno_l);

            status.local_cert_failures++;
            break;
        default:  
            retcode = WSREP_CONN_FAIL;
            gu_fatal("wsdb append failed: seqno_g %lld seqno_l %lld",
                     seqno_g, seqno_l);
            abort();
            break;
        }
    }
    else {
        // This cannot really happen. Perhaps we should abort in this case
        gu_warn ("Local action replicated with outdated seqno: "
                 "current seqno %lld, action seqno %lld", last_recved, seqno_g);
        // this situation is as good as cancelled transaction. See above.
        retcode = WSREP_TRX_FAIL;
        // commit queue will be cancelled below.
    }

    // call release only if grab was successfull
    GALERA_RELEASE_QUEUE (cert_queue, seqno_l);

    if (retcode == WSREP_OK) {
        assert (seqno_l >= 0);
	/* Grab commit queue for commit time */
        // can't use it here GALERA_GRAB_COMMIT_QUEUE (seqno_l);
        rcode = while_eagain_or_trx_abort(
            trx_id, gu_to_grab, commit_queue, seqno_l);
        switch (rcode) {
        case 0: break;
        case -ECANCELED:
	    gu_debug("canceled in commit queue for %llu", seqno_l);
            wsdb_assign_trx_state(trx_id, WSDB_TRX_ABORTING_REPL);
            GU_DBUG_RETURN(WSREP_TRX_FAIL);
            break;
        case -EINTR:
        {
            int retries = 0;
            struct job_worker  *applier;
            struct job_context *ctx = (struct job_context *) 
                gu_malloc(sizeof(struct job_context));

#define MAX_RETRIES 10

	    gu_debug("interrupted in commit queue for %llu", seqno_l);
            while ((applier = job_queue_new_worker(applier_queue, JOB_REPLAYING)
                   ) == NULL
            ) {
                if (retries++ == MAX_RETRIES) {
                    gu_warn("replaying is not possible, aborting node");
                    return WSREP_NODE_FAIL;
                }
                gu_warn("replaying job queue full, retrying");
                gu_info("workers, registered: %d, active: %d, max: %d",
                        applier_queue->registered_workers,
                        applier_queue->active_workers,
                        applier_queue->max_concurrent_workers
                );
                sleep(1);
            }

            /* register job already here, to prevent later slave transactions 
             * from applying before us
             */
            ctx->seqno = seqno_l;
            ctx->ws    = ws;
            ctx->type  = JOB_REPLAYING;
            /* just register the job, call does not block */
            job_queue_register_job(applier_queue, applier, (void *)ctx);

            wsdb_assign_trx_ws(trx_id, ws);
            wsdb_assign_trx_pos(trx_id, WSDB_TRX_POS_COMMIT_QUEUE);
            wsdb_assign_trx_state(trx_id, WSDB_TRX_MUST_REPLAY);
            wsdb_assign_trx_applier(trx_id, applier, (void *)ctx);

            GU_DBUG_RETURN(WSREP_BF_ABORT);
            break;
        }
        default:
            gu_fatal("Failed to grab commit queue for %llu", seqno_l);
            abort();
        }

        // we can update last seen trx counter already here
        wsdb_set_local_trx_committed(trx_id);
    } else {
        /* Cancel commit queue since we are going to rollback */
        GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
    }

cleanup:

    gu_free(data); // TODO: cache writeset for 
    // was referenced by wsdb_get_write_set() above
    wsdb_deref_seqno (ws->last_seen_trx);
    wsdb_write_set_free(ws);
    GU_DBUG_RETURN(retcode);
}

static enum wsrep_status mm_galera_append_query(
    wsrep_t *gh, const wsrep_trx_id_t trx_id, 
    const char *query, const time_t timeval, const uint32_t randseed
) {

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    errno = 0;

    switch (wsdb_append_query(trx_id, (char*)query, timeval, randseed)) {
    case WSDB_OK:              return WSREP_OK;
    case WSDB_ERR_TRX_UNKNOWN: return WSREP_TRX_FAIL;
    default:                   return WSREP_CONN_FAIL;
    }
    return WSREP_OK;
}

#ifdef UNUSED
static enum wsrep_status galera_append_row(
    wsrep_trx_id_t trx_id,
    uint16_t len,
    uint8_t *data
) {
    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    errno = 0;

    switch(wsdb_append_row(trx_id, len, data)) {
    case WSDB_OK:               return WSREP_OK;
    case WSDB_ERR_TRX_UNKNOWN:  return WSREP_TRX_FAIL;
    default:                    return WSREP_CONN_FAIL;
    }
}
#endif

static enum wsrep_status mm_galera_append_row_key(
    wsrep_t *gh,
    const wsrep_trx_id_t trx_id,
    const char    *dbtable,
    const size_t dbtable_len,
    const char *key,
    const size_t key_len,
    const enum wsrep_action action
) {
    struct wsdb_key_rec   wsdb_key;
    struct wsdb_table_key table_key;
    struct wsdb_key_part  key_part;
    char wsdb_action  = WSDB_ACTION_UPDATE;

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    errno = 0;

    /* TODO: make this setupping static, needs mutex protection */
    wsdb_key.key             = &table_key;
    table_key.key_part_count = 1;
    table_key.key_parts      = &key_part;
    key_part.type            = WSDB_TYPE_VOID;

    /* assign key info */
    wsdb_key.dbtable     = (char*)dbtable;
    wsdb_key.dbtable_len = dbtable_len;
    key_part.length      = key_len;
    key_part.data        = (uint8_t*)key;

    switch (action) {
    case WSREP_UPDATE: wsdb_action=WSDB_ACTION_UPDATE; break;
    case WSREP_DELETE: wsdb_action=WSDB_ACTION_DELETE; break;
    case WSREP_INSERT: wsdb_action=WSDB_ACTION_INSERT; break;
    }

    switch(wsdb_append_row_key(trx_id, &wsdb_key, wsdb_action)) {
    case WSDB_OK:               return WSREP_OK;
    case WSDB_ERR_TRX_UNKNOWN:  return WSREP_TRX_FAIL;
    default:                    return WSREP_CONN_FAIL;
    }
}

static enum wsrep_status mm_galera_set_variable(
    wsrep_t *gh,
    const wsrep_conn_id_t  conn_id,
    const char *key,  const  size_t key_len, 
    const char *query, const size_t query_len
) {
    char var[256];

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    /*
     * an ugly way to provide dynamic way to change galera_debug parameter.
     *
     * we catch here galera_debug variable change, and alter debugging state
     * correspondingly.
     *
     * Note that galera-set_variable() is called for every SET command in 
     * session. We should avoid calling this for all variables in RBR session 
     * and for many un-meaningful variables in SQL session. When these 
     * optimisations are made, galera_debug changing should be implemented
     * in some other way. We should have separate calls for storing session
     * variables for connections and for setting galera configuration variables.
     *
     */
    if (!strncmp(key, "wsrep_debug", key_len)) {
        char value[256];
        memset(value, '\0', 256);
        gu_debug("GALERA set value: %s" , value);
        strncpy(value, query, query_len);
        const char *set_query= "wsrep_debug=ON";

        if (strstr(value, set_query)) {
            gu_info("GALERA enabling debug logging: %s" , value);
            gu_conf_debug_on();
        } else {
          gu_info("GALERA disabling debug logging: %s %s" , value, set_query);
            gu_conf_debug_off();
        }
    }
    strncpy(var, key, key_len);

    gu_debug("GALERA set var: %s" , var);

    errno = 0;
    switch(wsdb_store_set_variable(conn_id, (char*)key, key_len, 
				   (char*)query, query_len)) {
    case WSDB_OK:              return WSREP_OK;
    case WSDB_ERR_TRX_UNKNOWN: return WSREP_TRX_FAIL;
    default:                   return WSREP_CONN_FAIL;
    }


    return WSREP_OK;
}

static enum wsrep_status mm_galera_set_database(
    wsrep_t *gh,
    const wsrep_conn_id_t conn_id, const char *query, const size_t query_len
) {

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    errno = 0;
    switch(wsdb_store_set_database(conn_id, (char*)query, query_len)) {
    case WSDB_OK:              return WSREP_OK;
    case WSDB_ERR_TRX_UNKNOWN: return WSREP_TRX_FAIL;
    default:                   return WSREP_CONN_FAIL;
    }
    return WSREP_OK;
}

static enum wsrep_status mm_galera_to_execute_start(
    wsrep_t *gh, const wsrep_conn_id_t conn_id, 
    const char *query, const size_t query_len
) {

    int                    rcode;
    struct wsdb_write_set* ws;
    XDR                    xdrs;
    int                    data_max = 34000; /* only fixed xdr buf supported */
    uint8_t                data[data_max];
    int                    len;
    gcs_seqno_t            seqno_g, seqno_l;
    bool                   do_apply;
    struct job_worker*     applier;
    struct job_context*    ctx;

    GU_DBUG_ENTER("galera_to_execute_start");

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_CONN_FAIL;

    GU_DBUG_PRINT("galera", ("conn: %llu", conn_id));

    /* retrieve write set */
    ws = wsdb_get_conn_write_set(conn_id);
    if (!ws) {
        GU_DBUG_RETURN(WSREP_CONN_FAIL);
    }

    /* append the query to be executed */
    wsdb_set_exec_query(ws, (char*)query, query_len);

    /* encode with xdr */
    /* TODO: is not optimal to allocate data buffer for xdr encoding
     *       intermediate result.
     *       Should use xdrrec stream instead and encode directly on
     *       gcs channel as we go.
     */
    memset(data, 0, data_max);
    xdrmem_create(&xdrs, (char *)data, data_max, XDR_ENCODE);
    if (!xdr_wsdb_write_set(&xdrs, ws)) {
        gu_error("xdr failed for: %llu", conn_id);
        GU_DBUG_RETURN(WSREP_CONN_FAIL);
    }
    len = xdr_getpos(&xdrs);

#ifdef GALERA_USE_FLOW_CONTROL
    while ((rcode = gcs_wait(gcs_conn)) && rcode > 0) {
        status.fc_waits++;
        usleep (GALERA_USLEEP_FLOW_CONTROL);
    }

    if (rcode < 0) {
        rcode = WSREP_CONN_FAIL; // in this case we better reconnect
        goto cleanup;
    }
#endif

    /* replicate through gcs */
    do {
        rcode = gcs_repl(gcs_conn, data, len, GCS_ACT_TORDERED, &seqno_g,
	                 &seqno_l);
    } while (-EAGAIN == rcode && (usleep (GALERA_USLEEP_FLOW_CONTROL), true));

    status.replicated++;
    status.replicated_bytes += len;

    if (rcode < 0) {
        gu_error("gcs_repl() failed for: %llu, %d (%s)",
                 conn_id, rcode, strerror (-rcode));
        assert (GCS_SEQNO_ILL == seqno_l);
        rcode = WSREP_CONN_FAIL;
        goto cleanup;
    }

    assert (GCS_SEQNO_ILL != seqno_g);
    assert (GCS_SEQNO_ILL != seqno_l);

    applier = job_queue_new_worker(applier_queue, JOB_TO_ISOLATION);
    if (!applier) {
        gu_error("galera, could not create TO isolation applier");
        gu_info("active_workers: %d, max_workers: %d",
            applier_queue->active_workers, applier_queue->max_concurrent_workers
        );
        return WSREP_NODE_FAIL;
    }
    wsdb_conn_set_worker(conn_id, applier);

    gu_debug("TO isolation applier starting, id: %d seqnos: %lld - %lld", 
             applier->id, seqno_g, seqno_l
    );

    ctx = (struct job_context *)gu_malloc(sizeof(struct job_context));
    ctx->seqno = seqno_l;
    ctx->ws    = ws;
    ctx->type  = JOB_TO_ISOLATION;

    job_queue_start_job(applier_queue, applier, (void *)ctx);

    /* wait for total order */
    GALERA_GRAB_QUEUE (cert_queue, seqno_l);
    
    /* update global seqno */
    if ((do_apply = galera_update_global_seqno (seqno_g))) {
        /* record local sequence number in connection info */
        wsdb_conn_set_seqno(conn_id, seqno_l, seqno_g);
    }

    if (do_apply) {
        /* Grab commit queue */
        GALERA_GRAB_QUEUE (commit_queue, seqno_l);
        rcode = WSREP_OK;
    }
    else {
        // theoretically it is possible with poorly written application
        // (trying to replicate something before completing state transfer)
        gu_warn ("Local action replicated with outdated seqno: "
                 "current seqno %lld, action seqno %lld", last_recved, seqno_g);
        
        GALERA_RELEASE_QUEUE (cert_queue, seqno_l);
        GALERA_SELF_CANCEL_QUEUE (commit_queue, seqno_l);
        // this situation is as good as failed gcs_repl() call.
        job_queue_end_job(applier_queue, applier);
        job_queue_remove_worker(applier_queue, applier);
        wsdb_conn_set_worker(conn_id, NULL);
        rcode = WSREP_CONN_FAIL;
    }

cleanup:

    wsdb_write_set_free(ws); // cache for incremental state transfer if applied
    gu_debug("TO isolation applied for: seqnos: %lld - %lld", 
             seqno_g, seqno_l
    );
    GU_DBUG_RETURN(rcode);
}

static enum wsrep_status mm_galera_to_execute_end(
    wsrep_t *gh, const wsrep_conn_id_t conn_id
) {
    bool do_report;
    struct wsdb_conn_info conn_info;
    int rcode;
    struct job_context *ctx;

    GU_DBUG_ENTER("galera_to_execute_end");

    if (gu_unlikely(conn_state != GALERA_CONNECTED)) return WSREP_OK;

    if ((rcode = wsdb_conn_get_info(conn_id, &conn_info) != WSDB_OK)) {
        gu_warn("missing connection for: %lld, rcode: %d", conn_id, rcode);
        GU_DBUG_RETURN(WSREP_CONN_FAIL);
    }

    gu_debug("TO applier ending  seqnos: %lld - %lld", 
             conn_info.seqno_l, conn_info.seqno_g
    );

    GALERA_UPDATE_LAST_APPLIED (conn_info.seqno_g)

    do_report = report_check_counter ();

    /* release queues */
    GALERA_RELEASE_QUEUE (cert_queue, conn_info.seqno_l);
    GALERA_RELEASE_QUEUE (commit_queue, conn_info.seqno_l);
    
    /* cleanup seqno reference */
    wsdb_conn_reset_seqno(conn_id);
    
    if (do_report) report_last_committed (gcs_conn);

    ctx = (struct job_context *)job_queue_end_job(
        applier_queue, conn_info.worker
    );
    assert(ctx);
    gu_free(ctx);

    job_queue_remove_worker(applier_queue, conn_info.worker);
    wsdb_conn_set_worker(conn_id, NULL);

    GU_DBUG_RETURN(WSDB_OK);
}

static enum wsrep_status mm_galera_replay_trx(
    wsrep_t *gh, const wsrep_trx_id_t trx_id, void *app_ctx
) {
    struct job_worker   *applier;
    struct job_context  ctx;
    
    int                rcode;
    enum wsrep_status  ret_code = WSREP_OK;
    wsdb_trx_info_t    trx;

    wsdb_get_local_trx_info(trx_id, &trx);

    gu_debug("trx_replay for: %lld %lld state: %d, rbr len: %d", 
            trx.seqno_l, trx.seqno_g, trx.state, trx.ws->rbr_buf_len);

    if (trx.state != WSDB_TRX_MUST_REPLAY) {
        gu_error("replayed trx in bad state: %d", trx.state);
        return WSREP_NODE_FAIL;
    }

    wsdb_assign_trx_state(trx_id, WSDB_TRX_REPLAYING);

    if (!trx.applier) {
        int retries = 0;
        assert (trx.position == WSDB_TRX_POS_CERT_QUEUE);

#define MAX_RETRIES 10
        while ((applier = job_queue_new_worker(applier_queue, JOB_REPLAYING)
                ) == NULL
        ) {
            if (retries++ == MAX_RETRIES) {
                gu_warn("replaying (cert) is not possible, aborting node");
                return WSREP_NODE_FAIL;
            }
            gu_warn("replaying job queue full, retrying");
            gu_info("workers, registered: %d, active: %d, max: %d",
                    applier_queue->registered_workers,
                    applier_queue->active_workers,
                    applier_queue->max_concurrent_workers
            );
            sleep(1);
        }

        /* register job already here, to prevent later slave transactions 
         * from applying before us. 
         * There will be a race condition between release of cert TO and 
         * job_queue_start_job() call.
         */
        ctx.seqno = trx.seqno_l;
        ctx.ws    = trx.ws;
        ctx.type  = JOB_REPLAYING;
        /* just register the job, call does not block */
        job_queue_register_job(applier_queue, applier, (void *)&ctx);

    } else {
        gu_debug("Using registered applier");
        assert (trx.position == WSDB_TRX_POS_COMMIT_QUEUE);
        applier = trx.applier;
    }
    gu_debug("replaying applier in to grab: %d, seqno: %lld %lld", 
             applier->id, trx.seqno_g, trx.seqno_l
    );

    /*
     * grab commit_queue already here, to make sure the conflicting BF applier
     * has completed before we start.
     * commit_queue will be released at post_commit(), which the application
     * has reponsibility to call.
     */
    rcode = while_eagain (gu_to_grab, commit_queue, trx.seqno_l);
    if (rcode) {
        gu_fatal("replaying commit queue grab failed for: %d seqno: %lld %lld", 
                 rcode, trx.seqno_g, trx.seqno_l);
        abort();
    }
    gu_debug("replaying applier starting");

    /*
     * tell app, that replaying will start with allocated seqno
     * when job is done, we will reset the seqno back to 0
     */
    ws_start_cb(app_ctx, trx.seqno_l);

    if (trx.ws->type == WSDB_WS_TYPE_TRX) {

        switch (trx.position) {
        case WSDB_TRX_POS_CERT_QUEUE:
            ret_code = process_query_write_set(
                applier, app_ctx, trx.ws, trx.seqno_g, trx.seqno_l
            );
            break;

        case WSDB_TRX_POS_COMMIT_QUEUE:
            ret_code = process_query_write_set_applying( 
              applier, app_ctx, trx.ws, trx.seqno_g, trx.seqno_l
            );

            /* register committed transaction */
            if (ret_code != WSREP_OK) {
                gu_fatal(
                     "could not re-apply trx: %lld %lld", 
                     trx.seqno_g, trx.seqno_l
                );
                abort();
            }
            break;
        default:
            gu_fatal("bad trx pos in reapplying: %d %lld", 
                     trx.position, trx.seqno_g, trx.seqno_l
            );
            abort();
        }
        job_queue_remove_worker(applier_queue, applier);
        if (trx.applier_ctx) {
            gu_free(trx.applier_ctx);
        }
    }
    else {
        gu_error("replayed trx ws has bad type: %d", trx.ws->type);
        ws_start_cb(app_ctx, 0);
        job_queue_remove_worker(applier_queue, applier);
        if (trx.applier_ctx) {
            gu_free(trx.applier_ctx);
        }
        return WSREP_NODE_FAIL;
    }
    wsdb_assign_trx_state(trx_id, WSDB_TRX_REPLICATED);

    ws_start_cb(app_ctx, 0);

    wsdb_deref_seqno (trx.ws->last_seen_trx);
    wsdb_write_set_free(trx.ws);

    gu_debug("replaying over for applier: %d rcode: %d, seqno: %lld %lld", 
             applier->id, rcode, trx.seqno_g, trx.seqno_l
    );

    // return only OK or TRX_FAIL
    return (ret_code == WSREP_OK) ? WSREP_OK : WSREP_TRX_FAIL;
}

static wsrep_status_t mm_galera_sst_sent (wsrep_t* gh,
                                          const wsrep_uuid_t* uuid,
                                          wsrep_seqno_t seqno)
{
    long err;

    if (memcmp (uuid, &status.state_uuid, sizeof(wsrep_uuid_t))) {
        // state we have sent no longer corresponds to the current group state
        // mark an error.
        seqno = -EREMCHG;
    }

    // WARNING: Here we have application block on this call which
    //          may prevent application from resolving the issue.
    //          (Not that we expect that application can resolve it.)
    while (-EAGAIN == (err = gcs_join (gcs_conn, seqno))) usleep (100000);

    status.stage = GALERA_STAGE_JOINED;

    if (!err) return WSREP_OK;

    return WSREP_CONN_FAIL;
}

static wsrep_status_t mm_galera_sst_received (wsrep_t* gh,
                                              const wsrep_uuid_t* uuid,
                                              wsrep_seqno_t seqno)
{
    gu_mutex_lock (&sst_mtx);
    {
        sst_uuid  = *(gu_uuid_t*)uuid;
        sst_seqno = seqno;
    }
    gu_cond_signal  (&sst_cond);
    gu_mutex_unlock (&sst_mtx);

    return WSREP_OK;
}

static struct wsrep_status_var* mm_galera_status_get (wsrep_t* gh)
{
    return galera_status_get (&status);
}

static void mm_galera_status_free (wsrep_t* gh,
                                   struct wsrep_status_var* s)
{
    galera_status_free (s);
}

static wsrep_t mm_galera_str = {
    WSREP_INTERFACE_VERSION,
    &mm_galera_init,
    &mm_galera_options_set,
    &mm_galera_options_get,
    &mm_galera_connect,
    &mm_galera_disconnect,
    &mm_galera_recv,
    &mm_galera_pre_commit,
    &mm_galera_post_commit,
    &mm_galera_post_rollback,
    &mm_galera_replay_trx,
    &mm_galera_abort_pre_commit,
    &mm_galera_abort_slave_trx,
    &mm_galera_append_query,
    &mm_galera_append_row_key,
    &mm_galera_set_variable,
    &mm_galera_set_database,
    &mm_galera_to_execute_start,
    &mm_galera_to_execute_end,
    &mm_galera_sst_sent,
    &mm_galera_sst_received,
    &mm_galera_status_get,
    &mm_galera_status_free,
    "Galera",
    "0.7pre",
    "Codership Oy <info@codership.com>",
    &mm_galera_tear_down,
    NULL,
    NULL
};

/* Prototype to make compiler happy */
int wsrep_loader(wsrep_t *hptr);

int wsrep_loader(wsrep_t *hptr)
{
    if (!hptr)
	return EINVAL;
    *hptr = mm_galera_str;
    return WSREP_OK;
}
