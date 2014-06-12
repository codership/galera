/* Copyright (C) 2010 Codership Oy <info@codersip.com> */

#include "replicator_smm.hpp"
#include "uuid.hpp"

// @todo: should be protected static member of the parent class
static const size_t GALERA_STAGE_MAX(11);
// @todo: should be protected static member of the parent class
static const char* state_str[GALERA_STAGE_MAX] =
{
    "Initialized",
    "Joining",
    "Joining: preparing for State Transfer",
    "Joining: requested State Transfer",
    "Joining: receiving State Transfer",
    "Joined",
    "Synced",
    "Donor/Desynced",
    "Joining: State Transfer request failed",
    "Joining: State Transfer failed",
    "Destroyed"
};

// @todo: should be protected static member of the parent class
static wsrep_member_status_t state2stats(galera::ReplicatorSMM::State state)
{
    switch (state)
    {
    case galera::ReplicatorSMM::S_DESTROYED :
    case galera::ReplicatorSMM::S_CLOSED    :
    case galera::ReplicatorSMM::S_CLOSING   :
    case galera::ReplicatorSMM::S_CONNECTED : return WSREP_MEMBER_UNDEFINED;
    case galera::ReplicatorSMM::S_JOINING   : return WSREP_MEMBER_JOINER;
    case galera::ReplicatorSMM::S_JOINED    : return WSREP_MEMBER_JOINED;
    case galera::ReplicatorSMM::S_SYNCED    : return WSREP_MEMBER_SYNCED;
    case galera::ReplicatorSMM::S_DONOR     : return WSREP_MEMBER_DONOR;
    }

    gu_throw_fatal << "invalid state " << state;
}

// @todo: should be protected static member of the parent class
static const char* state2stats_str(galera::ReplicatorSMM::State    state,
                                   galera::ReplicatorSMM::SstState sst_state)
{
    using galera::ReplicatorSMM;

    switch (state)
    {
    case galera::ReplicatorSMM::S_DESTROYED :
        return state_str[10];
    case galera::ReplicatorSMM::S_CLOSED :
    case galera::ReplicatorSMM::S_CLOSING:
    case galera::ReplicatorSMM::S_CONNECTED:
    {
        if (sst_state == ReplicatorSMM::SST_REQ_FAILED)  return state_str[8];
        else if (sst_state == ReplicatorSMM::SST_FAILED) return state_str[9];
        else                                             return state_str[0];
    }
    case galera::ReplicatorSMM::S_JOINING:
    {
        if (sst_state == ReplicatorSMM::SST_WAIT) return state_str[4];
        else                                      return state_str[1];
    }
    case galera::ReplicatorSMM::S_JOINED : return state_str[5];
    case galera::ReplicatorSMM::S_SYNCED : return state_str[6];
    case galera::ReplicatorSMM::S_DONOR  : return state_str[7];
    }

    gu_throw_fatal << "invalid state " << state;
}

typedef enum status_vars
{
    STATS_STATE_UUID = 0,
    STATS_PROTOCOL_VERSION,
    STATS_LAST_APPLIED,
    STATS_REPLICATED,
    STATS_REPLICATED_BYTES,
    STATS_KEYS_COUNT,
    STATS_KEYS_BYTES,
    STATS_DATA_BYTES,
    STATS_UNRD_BYTES,
    STATS_RECEIVED,
    STATS_RECEIVED_BYTES,
    STATS_LOCAL_COMMITS,
    STATS_LOCAL_CERT_FAILURES,
    STATS_LOCAL_REPLAYS,
    STATS_LOCAL_SEND_QUEUE,
    STATS_LOCAL_SEND_QUEUE_AVG,
    STATS_LOCAL_RECV_QUEUE,
    STATS_LOCAL_RECV_QUEUE_AVG,
    STATS_LOCAL_CACHED_DOWNTO,
    STATS_FC_PAUSED_NS,
    STATS_FC_PAUSED_AVG,
    STATS_FC_SENT,
    STATS_FC_RECEIVED,
    STATS_CERT_DEPS_DISTANCE,
    STATS_APPLY_OOOE,
    STATS_APPLY_OOOL,
    STATS_APPLY_WINDOW,
    STATS_APPLY_COUNT,
    STATS_COMMIT_OOOE,
    STATS_COMMIT_OOOL,
    STATS_COMMIT_WINDOW,
    STATS_COMMIT_COUNT,
    STATS_LOCAL_STATE,
    STATS_LOCAL_STATE_COMMENT,
    STATS_CERT_INDEX_SIZE,
    STATS_CAUSAL_READS,
    STATS_CERT_INTERVAL,
    STATS_INCOMING_LIST,
    STATS_MAX
} StatusVars;

static const struct wsrep_stats_var wsrep_stats[STATS_MAX + 1] =
{
    { "local_state_uuid",         WSREP_VAR_STRING, { 0 }  },
    { "protocol_version",         WSREP_VAR_INT64,  { 0 }  },
    { "last_committed",           WSREP_VAR_INT64,  { -1 } },
    { "replicated",               WSREP_VAR_INT64,  { 0 }  },
    { "replicated_bytes",         WSREP_VAR_INT64,  { 0 }  },
    { "repl_keys",                WSREP_VAR_INT64,  { 0 }  },
    { "repl_keys_bytes",          WSREP_VAR_INT64,  { 0 }  },
    { "repl_data_bytes",          WSREP_VAR_INT64,  { 0 }  },
    { "repl_other_bytes",         WSREP_VAR_INT64,  { 0 }  },
    { "received",                 WSREP_VAR_INT64,  { 0 }  },
    { "received_bytes",           WSREP_VAR_INT64,  { 0 }  },
    { "local_commits",            WSREP_VAR_INT64,  { 0 }  },
    { "local_cert_failures",      WSREP_VAR_INT64,  { 0 }  },
    { "local_replays",            WSREP_VAR_INT64,  { 0 }  },
    { "local_send_queue",         WSREP_VAR_INT64,  { 0 }  },
    { "local_send_queue_avg",     WSREP_VAR_DOUBLE, { 0 }  },
    { "local_recv_queue",         WSREP_VAR_INT64,  { 0 }  },
    { "local_recv_queue_avg",     WSREP_VAR_DOUBLE, { 0 }  },
    { "local_cached_downto",      WSREP_VAR_INT64,  { 0 }  },
    { "flow_control_paused_ns",   WSREP_VAR_INT64,  { 0 }  },
    { "flow_control_paused",      WSREP_VAR_DOUBLE, { 0 }  },
    { "flow_control_sent",        WSREP_VAR_INT64,  { 0 }  },
    { "flow_control_recv",        WSREP_VAR_INT64,  { 0 }  },
    { "cert_deps_distance",       WSREP_VAR_DOUBLE, { 0 }  },
    { "apply_oooe",               WSREP_VAR_DOUBLE, { 0 }  },
    { "apply_oool",               WSREP_VAR_DOUBLE, { 0 }  },
    { "apply_window",             WSREP_VAR_DOUBLE, { 0 }  },
    { "apply_count",              WSREP_VAR_INT64,  { 0 }  },
    { "commit_oooe",              WSREP_VAR_DOUBLE, { 0 }  },
    { "commit_oool",              WSREP_VAR_DOUBLE, { 0 }  },
    { "commit_window",            WSREP_VAR_DOUBLE, { 0 }  },
    { "commit_count",             WSREP_VAR_INT64,  { 0 }  },
    { "local_state",              WSREP_VAR_INT64,  { 0 }  },
    { "local_state_comment",      WSREP_VAR_STRING, { 0 }  },
    { "cert_index_size",          WSREP_VAR_INT64,  { 0 }  },
    { "causal_reads",             WSREP_VAR_INT64,  { 0 }  },
    { "cert_interval",            WSREP_VAR_DOUBLE, { 0 }  },
    { "incoming_addresses",       WSREP_VAR_STRING, { 0 }  },
    { 0,                          WSREP_VAR_STRING, { 0 }  }
};

void
galera::ReplicatorSMM::build_stats_vars (
    std::vector<struct wsrep_stats_var>& stats)
{
    const struct wsrep_stats_var* ptr(wsrep_stats);

    do
    {
        stats.push_back(*ptr);
    }
    while (ptr++->name != 0);

    stats[STATS_STATE_UUID].value._string = state_uuid_str_;
}

const struct wsrep_stats_var*
galera::ReplicatorSMM::stats_get() const
{
    if (S_DESTROYED == state_()) return 0;

    std::vector<struct wsrep_stats_var>& sv(wsrep_stats_);

    sv[STATS_PROTOCOL_VERSION   ].value._int64  = protocol_version_;
    sv[STATS_LAST_APPLIED       ].value._int64  = apply_monitor_.last_left();
    sv[STATS_REPLICATED         ].value._int64  = replicated_();
    sv[STATS_REPLICATED_BYTES   ].value._int64  = replicated_bytes_();
    sv[STATS_KEYS_COUNT         ].value._int64  = keys_count_();
    sv[STATS_KEYS_BYTES         ].value._int64  = keys_bytes_();
    sv[STATS_DATA_BYTES         ].value._int64  = data_bytes_();
    sv[STATS_UNRD_BYTES         ].value._int64  = unrd_bytes_();
    sv[STATS_RECEIVED           ].value._int64  = gcs_as_.received();
    sv[STATS_RECEIVED_BYTES     ].value._int64  = gcs_as_.received_bytes();
    sv[STATS_LOCAL_COMMITS      ].value._int64  = local_commits_();
    sv[STATS_LOCAL_CERT_FAILURES].value._int64  = local_cert_failures_();
    sv[STATS_LOCAL_REPLAYS      ].value._int64  = local_replays_();

    struct gcs_stats* ptr_stats = static_cast<struct gcs_stats*>(
        gu_malloc(sizeof(*ptr_stats)));
    if (!ptr_stats) return 0;
    struct gcs_stats& stats(*ptr_stats);
    gcs_.get_stats (&stats);

    sv[STATS_LOCAL_SEND_QUEUE    ].value._int64  = stats.send_q_len;
    sv[STATS_LOCAL_SEND_QUEUE_AVG].value._double = stats.send_q_len_avg;
    sv[STATS_LOCAL_RECV_QUEUE    ].value._int64  = stats.recv_q_len;
    sv[STATS_LOCAL_RECV_QUEUE_AVG].value._double = stats.recv_q_len_avg;
    sv[STATS_LOCAL_CACHED_DOWNTO ].value._int64  = gcache_.seqno_min();
    sv[STATS_FC_PAUSED_NS        ].value._int64  = stats.fc_paused_ns;
    sv[STATS_FC_PAUSED_AVG       ].value._double = stats.fc_paused_avg;
    sv[STATS_FC_SENT             ].value._int64  = stats.fc_sent;
    sv[STATS_FC_RECEIVED         ].value._int64  = stats.fc_received;


    double avg_cert_interval(0);
    double avg_deps_dist(0);
    size_t index_size(0);
    cert_.stats_get(avg_cert_interval, avg_deps_dist, index_size);

    sv[STATS_CERT_DEPS_DISTANCE  ].value._double = avg_deps_dist;
    sv[STATS_CERT_INTERVAL       ].value._double = avg_cert_interval;
    sv[STATS_CERT_INDEX_SIZE     ].value._int64 = index_size;

    double oooe;
    double oool;
    double win;
    size_t entered(0);
    const_cast<Monitor<ApplyOrder>&>(apply_monitor_).
        get_stats(&oooe, &oool, &win, &entered);

    sv[STATS_APPLY_OOOE          ].value._double = oooe;
    sv[STATS_APPLY_OOOL          ].value._double = oool;
    sv[STATS_APPLY_WINDOW        ].value._double = win;
    sv[STATS_APPLY_COUNT         ].value._int64 = entered;

    const_cast<Monitor<CommitOrder>&>(commit_monitor_).
        get_stats(&oooe, &oool, &win, &entered);

    sv[STATS_COMMIT_OOOE         ].value._double = oooe;
    sv[STATS_COMMIT_OOOL         ].value._double = oool;
    sv[STATS_COMMIT_WINDOW       ].value._double = win;
    sv[STATS_COMMIT_COUNT        ].value._int64 = entered;


    sv[STATS_LOCAL_STATE         ].value._int64  = state2stats(state_());
    sv[STATS_LOCAL_STATE_COMMENT ].value._string = state2stats_str(state_(),
                                                                   sst_state_);
    sv[STATS_CAUSAL_READS].value._int64    = causal_reads_();

    /* Create a buffer to be passed to the caller. */

    // the structure of return buffer
    // 1. fixed stats vars
    // 2. incomming list
    // 3. backend stats.
    // 4. pointer to gcs_stats

    // compute how many wsrep_stats_vars
    size_t all_sv_size(sv.size());
    struct gcs_backend_stats::stats_t* backend_stats = stats.backend_stats.stats;
    if (backend_stats) {
        for(int i=0; backend_stats[i].key; i++) {
            all_sv_size++;
        }
    }
    char* incoming_list_dup = NULL;
    {
        gu::Lock lock_inc(incoming_mutex_);
        incoming_list_dup = strndup(incoming_list_.c_str(),
                                    incoming_list_.size());
    }
    struct wsrep_stats_var* const buf(
        static_cast<struct wsrep_stats_var*>(
            gu_malloc(all_sv_size * sizeof(wsrep_stats_var) +
                      sizeof(ptr_stats))));

    if (buf)
    {
        memcpy(buf, &sv[0], sv.size() * sizeof(wsrep_stats_var));
        wsrep_stats_var* incoming_list_sv =
                static_cast<wsrep_stats_var*>(buf) +  STATS_INCOMING_LIST;
        incoming_list_sv->value._string = incoming_list_dup;
        wsrep_stats_var* next_sv = incoming_list_sv + 1;
        if (backend_stats) {
            for(int i=0; backend_stats[i].key; i++) {
                next_sv->name = backend_stats[i].key;
                next_sv->type = WSREP_VAR_STRING;
                next_sv->value._string = backend_stats[i].value;
                next_sv++;
            }
        }
        next_sv->name = NULL;
        next_sv->type = WSREP_VAR_STRING;
        next_sv->value._string = NULL;
        *reinterpret_cast<struct gcs_stats**>(next_sv + 1) = ptr_stats;
    }
    else
    {
        log_warn << "Failed to allocate stats vars buffer to "
                 << (all_sv_size * sizeof(wsrep_stats_var))
                 << " bytes. System is running out of memory.";

        // clear garbage.
        free(incoming_list_dup);
        gcs_.free_stats(ptr_stats);
    }

    return buf;
}

void
galera::ReplicatorSMM::stats_reset()
{
    if (S_DESTROYED == state_()) return;

    gcs_.flush_stats ();

    apply_monitor_.flush_stats();

    commit_monitor_.flush_stats();

    cert_.stats_reset();
}

void
galera::ReplicatorSMM::stats_free(struct wsrep_stats_var* arg)
{
    if (!arg) return;
    log_debug << "########### Freeing stats object ##########";
    wsrep_stats_var* incoming_list_sv = arg + STATS_INCOMING_LIST;
    log_debug << "free incomming list";
    free(const_cast<char*>(incoming_list_sv->value._string));
    wsrep_stats_var* next_sv = incoming_list_sv + 1;
    while(next_sv->name) next_sv++;
    struct gcs_stats* ptr_stats =
            *reinterpret_cast<struct gcs_stats**>(next_sv + 1);
    log_debug << "gcs free stats";
    gcs_.free_stats(ptr_stats);
    gu_free(ptr_stats);
    gu_free(arg);
}
