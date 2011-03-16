//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

//! @file replicator_smm.hpp
//
// @brief Galera Synchronous Multi-Master replicator
//

#ifndef GALERA_REPLICATOR_SMM_HPP
#define GALERA_REPLICATOR_SMM_HPP

#include "replicator.hpp"

#include "GCache.hpp"
#include "gcs.hpp"
#include "monitor.hpp"
#include "wsdb.hpp"
#include "certification.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"
#include "galera_service_thd.hpp"
#include "fsm.hpp"
#include "gcs_action_source.hpp"

#include "gu_atomic.hpp"

#include <map>

namespace galera
{
    class ReplicatorSMM : public Replicator
    {
    public:

        typedef enum
        {
            SST_NONE,
            SST_WAIT,
            SST_REQ_FAILED,
            SST_FAILED
        } SstState;

        static const size_t N_STATES = S_DONOR + 1;

        ReplicatorSMM(const wsrep_init_args* args);

        ~ReplicatorSMM();

        wsrep_status_t connect(const std::string& cluster_name,
                               const std::string& cluster_url,
                               const std::string& state_donor);
        wsrep_status_t close();
        wsrep_status_t async_recv(void* recv_ctx);

        TrxHandle* local_trx(wsrep_trx_id_t);
        TrxHandle* local_trx(wsrep_trx_handle_t*, bool);
        void unref_local_trx(TrxHandle* trx);
        void discard_local_trx(wsrep_trx_id_t trx_id);

        TrxHandle* local_conn_trx(wsrep_conn_id_t, bool);
        void set_default_context(wsrep_conn_id_t conn_id,
                                 const void* cxt, size_t cxt_len);
        void discard_local_conn_trx(wsrep_conn_id_t conn_id);
        void discard_local_conn(wsrep_conn_id_t conn_id);

        void apply_trx(void* recv_ctx, TrxHandle* trx)
            throw (ApplyException);

        wsrep_status_t replicate(TrxHandle* trx);
        wsrep_status_t abort_trx(TrxHandle* trx);
        wsrep_status_t pre_commit(TrxHandle*  trx);
        wsrep_status_t replay_trx(TrxHandle* trx, void* replay_ctx);

        wsrep_status_t post_commit(TrxHandle* trx);
        wsrep_status_t post_rollback(TrxHandle* trx);

        wsrep_status_t causal_read(wsrep_seqno_t*);
        wsrep_status_t to_isolation_begin(TrxHandle* trx);
        wsrep_status_t to_isolation_end(TrxHandle* trx);
        wsrep_status_t sst_sent(const wsrep_uuid_t& uuid,
                                wsrep_seqno_t seqno);
        wsrep_status_t sst_received(const wsrep_uuid_t& uuid,
                                    wsrep_seqno_t       seqno,
                                    const void*         state,
                                    size_t              state_len);
        void process_trx(void* recv_ctx, TrxHandle* trx)
            throw (ApplyException);
        void process_commit_cut(wsrep_seqno_t seq, wsrep_seqno_t seqno_l)
            throw (gu::Exception);
        void process_view_info(void* recv_ctx,
                               const wsrep_view_info_t& view_info,
                               State next_state,
                               wsrep_seqno_t seqno_l)
            throw (gu::Exception);
        void process_state_req(void* recv_ctx, const void* req,
                               size_t req_size, wsrep_seqno_t seqno_l,
                               wsrep_seqno_t donor_seq)
            throw (gu::Exception);
        void process_join(wsrep_seqno_t seqno_l)
            throw (gu::Exception);
        void process_sync(wsrep_seqno_t seqno_l)
            throw (gu::Exception);
        const struct wsrep_stats_var* stats() const;

        void           param_set (const std::string& key,
                                  const std::string& value)
            throw (gu::Exception, gu::NotFound);

        std::string    param_get (const std::string& key) const
            throw (gu::Exception, gu::NotFound);
        const gu::Config& params() const { return config_; }
        void store_state      (const std::string& file) const;
        void restore_state    (const std::string& file);
        void invalidate_state (const std::string& file) const;

    private:

        ReplicatorSMM(const ReplicatorSMM&);
        void operator=(const ReplicatorSMM&);

        void report_last_committed();

        void request_sst(const wsrep_uuid_t&, wsrep_seqno_t, const void*,
                         size_t)
            throw (gu::Exception);

        wsrep_status_t cert(TrxHandle* trx);
        wsrep_status_t cert_for_aborted(TrxHandle* trx);

        void update_state_uuid (const wsrep_uuid_t& u);

        class LocalOrder
        {
        public:

            LocalOrder(TrxHandle& trx)
                :
                seqno_(trx.local_seqno()),
                trx_(&trx)
            { }

            LocalOrder(wsrep_seqno_t seqno)
                :
                seqno_(seqno),
                trx_(0)
            { }

            void lock()   { if (trx_ != 0) trx_->lock();   }
            void unlock() { if (trx_ != 0) trx_->unlock(); }

            wsrep_seqno_t seqno() const { return seqno_; }

            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                return (last_left + 1 == seqno_);
            }

        private:

            wsrep_seqno_t seqno_;
            TrxHandle*    trx_;
        };

        class ApplyOrder
        {
        public:

            ApplyOrder(TrxHandle& trx) : trx_(trx) { }

            void lock()   { trx_.lock();   }
            void unlock() { trx_.unlock(); }

            wsrep_seqno_t seqno() const { return trx_.global_seqno(); }

            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                return (trx_.is_local() == true ||
                        last_left >= trx_.last_depends_seqno());
            }

        private:

            TrxHandle& trx_;
        };

        // state machine
        class Transition
        {
        public:

            Transition(State const from, State const to) :
                from_(from),
                to_(to)
            { }

            State from() const { return from_; }
            State to()   const { return to_;   }

            bool operator==(Transition const& other) const
            {
                return (from_ == other.from_ && to_ == other.to_);
            }

            class Hash
            {
            public:
                size_t operator()(Transition const& tr) const
                {
                    return (gu::HashValue(static_cast<int>(tr.from_))
                            ^ gu::HashValue(static_cast<int>(tr.to_)));
                }
            };

        private:

            State from_;
            State to_;
        };


        void build_stats_vars (std::vector<struct wsrep_stats_var>& stats);

        class Logger
        {
        public:
            Logger (gu_log_cb_t cb) { gu_conf_set_log_callback(cb); }
        };

        Logger                 logger_;
        gu::Config             config_;
        FSM<State, Transition> state_;
        SstState               sst_state_;

        // persistent data location
        std::string           data_dir_;
        std::string           state_file_;

        // identifiers
        wsrep_uuid_t          uuid_;
        wsrep_uuid_t const    state_uuid_;
        const char            state_uuid_str_[37];

        // application callbacks
        void*                 app_ctx_;
        wsrep_view_cb_t       view_cb_;
        wsrep_bf_apply_cb_t   bf_apply_cb_;
        wsrep_sst_donate_cb_t sst_donate_cb_;
        wsrep_synced_cb_t     synced_cb_;

        // SST
        std::string   sst_donor_;
        wsrep_uuid_t  sst_uuid_;
        wsrep_seqno_t sst_seqno_;
        gu::Mutex     sst_mutex_;
        gu::Cond      sst_cond_;
        int           sst_retry_sec_;

        // services
        gcache::GCache gcache_;
        Gcs            gcs_;
        ServiceThd     service_thd_;

        // action sources
        ActionSource*   as_;
        GcsActionSource gcs_as_;

        // trx processing
        Wsdb            wsdb_;
        Certification   cert_;

        // concurrency control
        Monitor<LocalOrder> local_monitor_;
        Monitor<ApplyOrder> apply_monitor_;

        // counters
        gu::Atomic<size_t>    receivers_;
        gu::Atomic<long long> replicated_;
        gu::Atomic<long long> replicated_bytes_;
        gu::Atomic<long long> local_commits_;
        gu::Atomic<long long> local_rollbacks_;
        gu::Atomic<long long> local_cert_failures_;
        gu::Atomic<long long> local_bf_aborts_;
        gu::Atomic<long long> local_replays_;

        // reporting last committed
        size_t             report_interval_;
        gu::Atomic<size_t> report_counter_;

        mutable std::vector<struct wsrep_stats_var> wsrep_stats_;
    };

    std::ostream& operator<<(std::ostream& os, ReplicatorSMM::State state);
}

#endif /* GALERA_REPLICATOR_SMM_HPP */
