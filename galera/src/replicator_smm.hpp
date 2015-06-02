//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

//! @file replicator_smm.hpp
//
// @brief Galera Synchronous Multi-Master replicator
//

#ifndef GALERA_REPLICATOR_SMM_HPP
#define GALERA_REPLICATOR_SMM_HPP

#include "replicator.hpp"

#include "gu_init.h"
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
#include "ist.hpp"
#include "gu_atomic.hpp"
#include "saved_state.hpp"
#include "gu_debug_sync.hpp"


#include <map>

namespace galera
{
    class ReplicatorSMM : public Replicator, public ist::ActionHandler
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

        int trx_proto_ver() const { return trx_params_.version_; }
        int repl_proto_ver() const{ return protocol_version_; }

        wsrep_status_t connect(const std::string& cluster_name,
                               const std::string& cluster_url,
                               const std::string& state_donor,
                               bool               bootstrap);
        wsrep_status_t close();
        wsrep_status_t async_recv(void* recv_ctx);

        TrxHandleMaster* get_local_trx(wsrep_trx_id_t trx_id, bool create = false)
        {
            return wsdb_.get_trx(trx_params_, uuid_, trx_id, create);
        }

        void unref_local_trx(TrxHandleMaster* trx)
        {
            assert(trx->refcnt() > 1);
            trx->unref();
        }

        void discard_local_trx(TrxHandleMaster* trx)
        {
// normally this should be done immediately after relication, otherwise - by dtor            trx->release_write_set_out();
            wsdb_.discard_trx(trx->trx_id());
        }

        TrxHandleMaster* local_conn_trx(wsrep_conn_id_t conn_id, bool create)
        {
            return wsdb_.get_conn_query(trx_params_, uuid_, conn_id, create);
        }

        void discard_local_conn_trx(wsrep_conn_id_t conn_id)
        {
            wsdb_.discard_conn_query(conn_id);
        }

        void discard_local_conn(wsrep_conn_id_t conn_id)
        {
            wsdb_.discard_conn(conn_id);
        }

        void apply_trx(void* recv_ctx, TrxHandleSlave* trx);

        wsrep_status_t replicate(TrxHandleMaster* trx, wsrep_trx_meta_t*);
        void           abort_trx(TrxHandleMaster* trx);
        wsrep_status_t pre_commit(TrxHandleMaster*  trx, wsrep_trx_meta_t*);
        wsrep_status_t replay_trx(TrxHandleMaster* trx, void* replay_ctx);

        wsrep_status_t post_commit(TrxHandleMaster* trx);
        wsrep_status_t post_rollback(TrxHandleMaster* trx);

        wsrep_status_t causal_read(wsrep_gtid_t*);
        wsrep_status_t to_isolation_begin(TrxHandleMaster* trx,
                                          wsrep_trx_meta_t*);
        wsrep_status_t to_isolation_end(TrxHandleMaster* trx);
        wsrep_status_t preordered_collect(wsrep_po_handle_t&      handle,
                                          const struct wsrep_buf* data,
                                          size_t                  count,
                                          bool                    copy);
        wsrep_status_t preordered_commit(wsrep_po_handle_t&      handle,
                                         const wsrep_uuid_t&     source,
                                         uint64_t                flags,
                                         int                     pa_range,
                                         bool                    commit);
        wsrep_status_t sst_sent(const wsrep_gtid_t& state_id, int rcode);
        wsrep_status_t sst_received(const wsrep_gtid_t& state_id,
                                    const void*         state,
                                    size_t              state_len,
                                    int                 rcode);

        void process_trx(void* recv_ctx, TrxHandleSlave* trx);
        void process_commit_cut(wsrep_seqno_t seq, wsrep_seqno_t seqno_l);
        void process_conf_change(void* recv_ctx, const struct gcs_action& cc);
        void process_state_req(void* recv_ctx, const void* req,
                               size_t req_size, wsrep_seqno_t seqno_l,
                               wsrep_seqno_t donor_seq);
        void process_join(wsrep_seqno_t seqno, wsrep_seqno_t seqno_l);
        void process_sync(wsrep_seqno_t seqno_l);

        const struct wsrep_stats_var* stats_get()  const;
        void                          stats_reset();
        void                   stats_free(struct wsrep_stats_var*);

        /*! @throws NotFound */
        void           set_param (const std::string& key,
                                  const std::string& value);

        /*! @throws NotFound */
        void           param_set (const std::string& key,
                                  const std::string& value);

        std::string    param_get (const std::string& key) const;

        const gu::Config& params() const { return config_; }

        wsrep_seqno_t pause();
        void          resume();

        void          desync();
        void          resync();

        const wsrep_uuid_t& source_id() const
        {
            return uuid_;
        }

        void preload_index(const gcs_action&);
        void wait(const wsrep_seqno_t&);
        void drain_monitors(const wsrep_seqno_t&);

        struct InitConfig
        {
            InitConfig(gu::Config&, const char* node_address, const char *base_dir);
        };

    private:

        ReplicatorSMM(const ReplicatorSMM&);
        void operator=(const ReplicatorSMM&);

        struct Param
        {
            static const std::string base_host;
            static const std::string base_port;
            static const std::string base_dir;
            static const std::string proto_max;
            static const std::string key_format;
            static const std::string commit_order;
            static const std::string causal_read_timeout;
            static const std::string max_write_set_size;
        };

        typedef std::pair<std::string, std::string> Default;

        struct Defaults
        {
            std::map<std::string, std::string> map_;
            Defaults ();
        };

        static const Defaults defaults;
        // both a list of parameters and a list of default values

        wsrep_seqno_t last_committed()
        {
            return co_mode_ != CommitOrder::BYPASS ?
                   commit_monitor_.last_left() : apply_monitor_.last_left();
        }

        void report_last_committed(wsrep_seqno_t purge_seqno)
        {
            if (gu_unlikely(purge_seqno != -1))
            {
                service_thd_.report_last_committed(purge_seqno);
            }
        }

        wsrep_status_t cert             (TrxHandleSlave* trx);
        wsrep_status_t cert_and_catch   (TrxHandleSlave* trx);
        wsrep_status_t cert_for_aborted (TrxHandleSlave* trx);

        void update_state_uuid    (const wsrep_uuid_t& u);
        void update_incoming_list (const wsrep_view_info_t& v);

        /* aborts/exits the program in a clean way */
        void abort() GU_NORETURN;

        class LocalOrder
        {
        public:

            explicit
            LocalOrder(const TrxHandleSlave& trx)
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

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (trx_ != 0 && trx_->is_local())
                {
                    unlock();
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("local_monitor_enter_sync");
                    mutex.lock();
                    lock();
                }
            }
#endif // GU_DBUG_ON
        private:

            LocalOrder(const LocalOrder&);
            wsrep_seqno_t const seqno_;
            const TrxHandleSlave* const trx_;
        };

        class ApplyOrder
        {
        public:

            explicit
            ApplyOrder(const TrxHandleSlave& trx) : trx_(trx) { }

            void lock()   { trx_.lock();   }
            void unlock() { trx_.unlock(); }

            wsrep_seqno_t seqno() const { return trx_.global_seqno(); }

            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                return (trx_.is_local() == true ||
                        last_left >= trx_.depends_seqno());
            }

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (trx_.is_local())
                {
                    unlock();
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("apply_monitor_enter_sync");
                    mutex.lock();
                    lock();
                }
            }
#endif // GU_DBUG_ON

        private:

            ApplyOrder(const ApplyOrder&);
            const TrxHandleSlave& trx_;
        };

    public:

        class CommitOrder
        {
        public:
            typedef enum
            {
                BYPASS     = 0,
                OOOC       = 1,
                LOCAL_OOOC = 2,
                NO_OOOC    = 3
            } Mode;

            static Mode from_string(const std::string& str)
            {
                int ret(gu::from_string<int>(str));
                switch (ret)
                {
                case BYPASS:
                case OOOC:
                case LOCAL_OOOC:
                case NO_OOOC:
                    break;
                default:
                    gu_throw_error(EINVAL)
                        << "invalid value " << str << " for commit order mode";
                }
                return static_cast<Mode>(ret);
            }

            CommitOrder(const TrxHandleSlave& trx, Mode mode)
                :
                trx_ (trx ),
                mode_(mode)
            { }

            void lock()   { trx_.lock();   }
            void unlock() { trx_.unlock(); }
            wsrep_seqno_t seqno() const { return trx_.global_seqno(); }
            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                switch (mode_)
                {
                case BYPASS:
                    gu_throw_fatal
                        << "commit order condition called in bypass mode";
                case OOOC:
                    return true;
                case LOCAL_OOOC:
                    return trx_.is_local();
                    // in case of remote trx fall through
                case NO_OOOC:
                    return (last_left + 1 == trx_.global_seqno());
                }
                gu_throw_fatal << "invalid commit mode value " << mode_;
            }

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (trx_.is_local())
                {
                    unlock();
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("commit_monitor_enter_sync");
                    mutex.lock();
                    lock();
                }
            }
#endif // GU_DBUG_ON

        private:
            CommitOrder(const CommitOrder&);
            const TrxHandleSlave& trx_;
            const Mode mode_;
        };

        class StateRequest
        {
        public:
            virtual const void* req     () const = 0;
            virtual ssize_t     len     () const = 0;
            virtual const void* sst_req () const = 0;
            virtual ssize_t     sst_len () const = 0;
            virtual const void* ist_req () const = 0;
            virtual ssize_t     ist_len () const = 0;
            virtual ~StateRequest() {}
        };

    private:
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

        void cancel_seqno(const wsrep_seqno_t& seqno);

        void set_initial_position(const wsrep_uuid_t&, const wsrep_seqno_t&);

        void establish_protocol_versions (int version);

        bool state_transfer_required(const wsrep_view_info_t& view_info,
                                     bool rejoined);

        void prepare_for_IST (void*& req, ssize_t& req_len,
                              const wsrep_uuid_t& group_uuid,
                              wsrep_seqno_t       group_seqno);

        void recv_IST(void* recv_ctx);
        bool process_IST_writeset(void* recv_ctx, const gcs_action& act);

        StateRequest* prepare_state_request (const void* sst_req,
                                             ssize_t     sst_req_len,
                                             const wsrep_uuid_t& group_uuid,
                                             wsrep_seqno_t       group_seqno);

        void send_state_request (const StateRequest* req);

        void request_state_transfer (void* recv_ctx,
                                     const wsrep_uuid_t& group_uuid,
                                     wsrep_seqno_t       group_seqno,
                                     const void*         sst_req,
                                     ssize_t             sst_req_len);

        void preload_index_trx(const gcs_action& act);
        void preload_index_cc(const gcs_action& act);

        /* These methods facilitate closing procedure.
         * They must be called under closing_mutex_ lock */
        void start_closing();
        void shift_to_CLOSED();
        void wait_for_CLOSED(gu::Lock&);

        /* local state seqno for internal use (macro mock up) */
        wsrep_seqno_t STATE_SEQNO(void) { return apply_monitor_.last_left(); }

        class InitLib /* Library initialization routines */
        {
        public:
            InitLib (gu_log_cb_t cb) { gu_init(cb); }
        };

        InitLib                init_lib_;
        gu::Config             config_;

        InitConfig
            init_config_; // registers configurable parameters and defaults

        struct ParseOptions
        {
            ParseOptions(Replicator& repl, gu::Config&, const char* opts);
        }
            parse_options_; // parse option string supplied on initialization

        class InitSSL
        {
        public:
            InitSSL(gu::Config& conf) { gu::ssl_init_options(conf); }
        } init_ssl_; // initialize global SSL parameters

        static int const       MAX_PROTO_VER;
        /*
         * |------------------------------------------------------
         * | protocol_version_ |  trx  version  | str_proto_ver_ |
         * |------------------------------------------------------
         * |                 1 |              1 |              0 |
         * |                 2 |              1 |              1 |
         * |                 3 |              2 |              1 |
         * |                 4 |              2 |              1 | v2.1
         * |                 5 |              3 |              1 |
         * |                 6 |              3 |              2 |
         * |                 7 |              3 |              2 |
         * |                 8 |              4 |              3 | + CC events
         * -------------------------------------------------------
         */

        int                    str_proto_ver_;// state transfer request protocol
        int                    protocol_version_;// general repl layer proto
        int                    proto_max_;    // maximum allowed proto version

        FSM<State, Transition> state_;
        gu::Mutex              closing_mutex_; // to sync close() call
        gu::Cond               closing_cond_;
        bool                   closing_; // to indicate that the closing process
                                         // started
        SstState               sst_state_;

        // configurable params
        const CommitOrder::Mode co_mode_; // commit order mode

        // persistent data location
        std::string           state_file_;
        SavedState            st_;

        // currently installed trx parameters
        TrxHandleMaster::Params trx_params_;

        // identifiers
        wsrep_uuid_t          uuid_;
        wsrep_uuid_t const    state_uuid_;
        const char            state_uuid_str_[37];
        wsrep_seqno_t         cc_seqno_; // seqno of last CC
        wsrep_seqno_t         cc_lowest_trx_seqno_;
        wsrep_seqno_t         pause_seqno_; // local seqno of last pause call

        // application callbacks
        void*                  app_ctx_;
        wsrep_connected_cb_t   connected_cb_;
        wsrep_view_cb_t        view_cb_;
        wsrep_sst_request_cb_t sst_request_cb_;
        wsrep_apply_cb_t       apply_cb_;
        wsrep_commit_cb_t      commit_cb_;
        wsrep_unordered_cb_t   unordered_cb_;
        wsrep_sst_donate_cb_t  sst_donate_cb_;
        wsrep_synced_cb_t      synced_cb_;

        // SST
        std::string   sst_donor_;
        wsrep_uuid_t  sst_uuid_;
        wsrep_seqno_t sst_seqno_;
        gu::Mutex     sst_mutex_;
        gu::Cond      sst_cond_;
        int           sst_retry_sec_;
        bool          sst_received_;

        // services
        gcache::GCache gcache_;
        GCS_IMPL       gcs_;
        ServiceThd     service_thd_;

        // action sources
        TrxHandleSlave::Pool slave_pool_;
        ActionSource*        as_;
        GcsActionSource      gcs_as_;
        ist::Receiver        ist_receiver_;
        ist::AsyncSenderMap  ist_senders_;

        // trx processing
        Wsdb            wsdb_;
        Certification   cert_;

        // concurrency control
        Monitor<LocalOrder>  local_monitor_;
        Monitor<ApplyOrder>  apply_monitor_;
        Monitor<CommitOrder> commit_monitor_;
        gu::datetime::Period causal_read_timeout_;

        // counters
        gu::Atomic<size_t>    receivers_;
        gu::Atomic<long long> replicated_;
        gu::Atomic<long long> replicated_bytes_;
        gu::Atomic<long long> keys_count_;
        gu::Atomic<long long> keys_bytes_;
        gu::Atomic<long long> data_bytes_;
        gu::Atomic<long long> unrd_bytes_;
        gu::Atomic<long long> local_commits_;
        gu::Atomic<long long> local_rollbacks_;
        gu::Atomic<long long> local_cert_failures_;
        gu::Atomic<long long> local_replays_;
        gu::Atomic<long long> causal_reads_;

        gu::Atomic<long long> preordered_id_; // temporary preordered ID

        // non-atomic stats
        std::string           incoming_list_;
        mutable gu::Mutex     incoming_mutex_;

        mutable std::vector<struct wsrep_stats_var> wsrep_stats_;
    };

    std::ostream& operator<<(std::ostream& os, ReplicatorSMM::State state);
}

#endif /* GALERA_REPLICATOR_SMM_HPP */
