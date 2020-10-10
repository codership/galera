//
// Copyright (C) 2010-2020 Codership Oy <info@codership.com>
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
#include "action_source.hpp"
#include "ist.hpp"
#include "gu_atomic.hpp"
#include "saved_state.hpp"
#include "gu_debug_sync.hpp"


#include <map>
#include <queue>

namespace galera
{
    class ReplicatorSMM : public Replicator, public ist::EventHandler
    {
    public:

        typedef enum
        {
            SST_NONE,
            SST_WAIT,
            SST_JOIN_SENT,
            SST_REQ_FAILED,
            SST_FAILED
        } SstState;

        static const size_t N_STATES = S_DONOR + 1;

        ReplicatorSMM(const wsrep_init_args* args);

        ~ReplicatorSMM();

        wsrep_cap_t capabilities() const { return capabilities(proto_max_); }
        int trx_proto_ver() const { return trx_params_.version_; }
        int repl_proto_ver() const{ return protocol_version_; }

        wsrep_status_t connect(const std::string& cluster_name,
                               const std::string& cluster_url,
                               const std::string& state_donor,
                               bool               bootstrap);
        wsrep_status_t close();
        wsrep_status_t async_recv(void* recv_ctx);

        TrxHandleMasterPtr get_local_trx(wsrep_trx_id_t trx_id,
                                         bool create = false)
        {
            return wsdb_.get_trx(trx_params_, uuid_, trx_id, create);
        }

        TrxHandleMasterPtr new_trx(const wsrep_uuid_t& uuid, wsrep_trx_id_t trx_id)
        {
            return wsdb_.new_trx(trx_params_, uuid, trx_id);
        }

        TrxHandleMasterPtr new_local_trx(wsrep_trx_id_t trx_id)
        {
            return new_trx(uuid_, trx_id);
        }

        void discard_local_trx(TrxHandleMaster* trx)
        {
            wsdb_.discard_trx(trx->trx_id());
        }

        TrxHandleMasterPtr local_conn_trx(wsrep_conn_id_t conn_id, bool create)
        {
            return wsdb_.get_conn_query(trx_params_, uuid_, conn_id, create);
        }

        void discard_local_conn_trx(wsrep_conn_id_t conn_id)
        {
            wsdb_.discard_conn_query(conn_id);
        }

        void apply_trx(void* recv_ctx, TrxHandleSlave& trx);
        wsrep_status_t handle_apply_error(TrxHandleSlave&    trx,
                                          const wsrep_buf_t& error_buf,
                                          const std::string& custom_msg);
        void process_apply_error(TrxHandleSlave&, const wsrep_buf_t&);

        wsrep_status_t send(TrxHandleMaster& trx, wsrep_trx_meta_t*);
        wsrep_status_t replicate(TrxHandleMaster& trx, wsrep_trx_meta_t*);
        wsrep_status_t abort_trx(TrxHandleMaster& trx, wsrep_seqno_t bf_seqno,
                                 wsrep_seqno_t* victim_seqno);
        wsrep_status_t certify(TrxHandleMaster& trx, wsrep_trx_meta_t*);
        wsrep_status_t commit_order_enter_local(TrxHandleMaster& trx);
        wsrep_status_t commit_order_enter_remote(TrxHandleSlave& trx);
        wsrep_status_t commit_order_leave(TrxHandleSlave& trx,
                                          const wsrep_buf_t*  error);
        wsrep_status_t release_commit(TrxHandleMaster& trx);
        wsrep_status_t release_rollback(TrxHandleMaster& trx);
        wsrep_status_t replay_trx(TrxHandleMaster& trx,
                                  TrxHandleLock& lock,
                                  void* replay_ctx);

        wsrep_status_t sync_wait(wsrep_gtid_t* upto,
                                 int           tout,
                                 wsrep_gtid_t* gtid);
        wsrep_status_t last_committed_id(wsrep_gtid_t* gtid) const;

        wsrep_status_t to_isolation_begin(TrxHandleMaster&  trx,
                                          wsrep_trx_meta_t* meta);
        wsrep_status_t to_isolation_end(TrxHandleMaster&   trx,
                                        const wsrep_buf_t* err);
        wsrep_status_t preordered_collect(wsrep_po_handle_t&      handle,
                                          const struct wsrep_buf* data,
                                          size_t                  count,
                                          bool                    copy);
        wsrep_status_t preordered_commit(wsrep_po_handle_t&       handle,
                                         const wsrep_uuid_t&      source,
                                         uint64_t                 flags,
                                         int                      pa_range,
                                         bool                     commit);
        wsrep_status_t sst_sent(const wsrep_gtid_t& state_id, int rcode);
        wsrep_status_t sst_received(const wsrep_gtid_t& state_id,
                                    const wsrep_buf_t*  state,
                                    int                 rcode);

        void process_trx(void* recv_ctx, const TrxHandleSlavePtr& trx);
        void process_commit_cut(wsrep_seqno_t seq, wsrep_seqno_t seqno_l);
        void submit_view_info(void* recv_ctx, const wsrep_view_info_t* cc);
        void process_conf_change(void* recv_ctx, const struct gcs_action& cc);
        void process_state_req(void* recv_ctx, const void* req,
                               size_t req_size, wsrep_seqno_t seqno_l,
                               wsrep_seqno_t donor_seq);
        void process_join(wsrep_seqno_t seqno, wsrep_seqno_t seqno_l);
        void process_sync(wsrep_seqno_t seqno_l);
        void process_vote(wsrep_seqno_t seq, int64_t code,wsrep_seqno_t seqno_l);

        const struct wsrep_stats_var* stats_get()  const;
        void                          stats_reset();
        void                          stats_free(struct wsrep_stats_var*);

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

        // IST Action handler interface
        void ist_trx(const TrxHandleSlavePtr& ts, bool must_apply,
                     bool preload);
        void ist_cc(const gcs_action&, bool must_apply, bool preload);
        void ist_end(int error);

        // Cancel local and enter apply monitors for TrxHandle
        void cancel_monitors_for_local(const TrxHandleSlave& ts)
        {
            log_debug << "canceling monitors on behalf of trx: " << ts;
            assert(ts.local());
            assert(ts.global_seqno() > 0);

            LocalOrder lo(ts);
            local_monitor_.self_cancel(lo);
        }

        // Cancel all monitors for given seqnos
        void cancel_seqnos(wsrep_seqno_t seqno_l, wsrep_seqno_t seqno_g);

        // Drain apply and commit monitors up to seqno
        void drain_monitors(wsrep_seqno_t seqno);

        class ISTEvent
        {
        public:
            enum Type
            {
                T_NULL, // empty
                T_TRX,  // TrxHandleSlavePtr
                T_VIEW  // configuration change
            };

            ISTEvent()
                : ts_()
                , view_()
                , type_(T_NULL)
            { }
            ISTEvent(const TrxHandleSlavePtr& ts)
                : ts_(ts)
                , view_()
                , type_(T_TRX)
            { }
            ISTEvent(wsrep_view_info_t* view)
                : ts_()
                , view_(view)
                , type_(T_VIEW)
            { }
            ISTEvent(const ISTEvent& other)
                : ts_(other.ts_)
                , view_(other.view_)
                , type_(other.type_)
            { }
            ISTEvent& operator=(const ISTEvent& other)
            {
                ts_ = other.ts_;
                view_ = other.view_;
                type_ = other.type_;
                return *this;
            }
            ~ISTEvent()
            { }
            Type type() const { return type_; }
            TrxHandleSlavePtr ts() const
            { assert(T_TRX == type_); return ts_; }
            wsrep_view_info_t* view() const
            { assert(T_VIEW == type_); return view_; }
        private:
            TrxHandleSlavePtr  ts_;
            wsrep_view_info_t* view_;
            Type               type_;
        };
        // Helper class to synchronize between IST receiver thread
        // applier threads.
        class ISTEventQueue
        {
        public:
            ISTEventQueue()
                :
                mutex_(),
                cond_(),
                eof_(false),
                error_(0),
                queue_()
            { }
            void reset() { eof_ = false; error_ = 0; }
            void eof(int error)
            {
                gu::Lock lock(mutex_);
                eof_ = true;
                error_ = error;
                cond_.broadcast();
            }

            // Push back
            void push_back(const TrxHandleSlavePtr& ts)
            {
                gu::Lock lock(mutex_);
                queue_.push(ISTEvent(ts));
                cond_.signal();
            }

            // Push back
            void push_back(wsrep_view_info_t* view)
            {
                gu::Lock lock(mutex_);
                queue_.push(ISTEvent(view));
                cond_.signal();
            }

            // Pop front
            //
            // Throws gu::Exception() in case of error for the first
            // caller which will detect the error.
            // Returns null in case of EOF
            ISTEvent pop_front()
            {
                gu::Lock lock(mutex_);
                while (eof_ == false && queue_.empty() == true)
                {
                    lock.wait(cond_);
                }

                ISTEvent ret;
                if (queue_.empty() == false)
                {
                    ret = queue_.front();
                    queue_.pop();
                }
                else
                {
                    if (error_)
                    {
                        int err(error_);
                        error_ = 0; // Make just one thread to detect the failure
                        gu_throw_error(err)
                            << "IST receiver reported failure";
                    }
                }

                return ret;
            }

        private:
            gu::Mutex mutex_;
            gu::Cond  cond_;
            bool eof_;
            int error_;
            std::queue<ISTEvent> queue_;
        };


        ISTEventQueue ist_event_queue_;

        void mark_corrupt_and_close()
        /* mark state as corrupt and try to leave cleanly */
        {
            st_.mark_corrupt();
            gu::Lock lock(closing_mutex_);
            start_closing();
        }

        void on_inconsistency()
        {
            cert_.mark_inconsistent();
            mark_corrupt_and_close();
        }

        bool corrupt() const { return st_.corrupt(); }

        struct InitConfig
        {
            InitConfig(gu::Config&, const char* node_addr,const char* base_dir);
        };

        class StateRequest
        {
        public:
            virtual int         version () const = 0;
            virtual const void* req     () const = 0;
            virtual ssize_t     len     () const = 0;
            virtual const void* sst_req () const = 0;
            virtual ssize_t     sst_len () const = 0;
            virtual const void* ist_req () const = 0;
            virtual ssize_t     ist_len () const = 0;
            virtual ~StateRequest() {}
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

        static wsrep_cap_t capabilities(int protocol_version);

        // Return the global seqno of the last transaction which has
        // commmitted.
        //
        // galera-bugs#555: Assign last seen from apply_monitor_.last_left()
        // because apply monitor is held until the whole transaction is over.
        // Commit monitor may be released early due to group commit.
        wsrep_seqno_t last_committed()
        {
            return apply_monitor_.last_left();
        }

        void report_last_committed(wsrep_seqno_t purge_seqno)
        {
            if (gu_unlikely(purge_seqno != -1))
            {
                service_thd_.report_last_committed(purge_seqno);
            }
        }
        // Helpers for configuration change processing
        void drain_monitors_for_local_conf_change();
        void process_non_prim_conf_change(void* recv_ctx,
                                          const gcs_act_cchange&,
                                          int my_index);
        bool skip_prim_conf_change(const wsrep_view_info_t& view,
                                   int group_proto_ver);
        void process_first_view(const wsrep_view_info_t*, const wsrep_uuid_t&);
        void process_group_change(const wsrep_view_info_t*);
        void process_st_required(void* recv_ctx, int group_proto_ver,
                                 const wsrep_view_info_t*);
        void reset_index_if_needed(const wsrep_view_info_t* view_info,
                                   int prev_protocol_version,
                                   int next_protocol_version,
                                   bool st_required);
        void shift_to_next_state(Replicator::State next_state);
        void become_joined_if_needed();
        void submit_ordered_view_info(void* recv_ctx, const wsrep_view_info_t*);
        void finish_local_prim_conf_change(int group_proto_ver,
                                           wsrep_seqno_t seqno,
                                           const char* context);
        void process_prim_conf_change(void* recv_ctx,
                                      const gcs_act_cchange&,
                                      int my_index,
                                      void* cc_buf);
        void process_ist_conf_change(const gcs_act_cchange&);
        TrxHandleSlavePtr get_real_ts_with_gcache_buffer(const TrxHandleSlavePtr&);
        void handle_trx_overlapping_ist(const TrxHandleSlavePtr& ts);

        // Helpers for IST processing.
        void handle_ist_nbo(const TrxHandleSlavePtr& ts, bool must_apply,
                            bool preload);
        void handle_ist_trx_preload(const TrxHandleSlavePtr& ts,
                                    bool must_apply);
        void handle_ist_trx(const TrxHandleSlavePtr& ts, bool must_apply,
                            bool preload);

        /* process pending queue events scheduled before local_seqno */
        void process_pending_queue(wsrep_seqno_t local_seqno);

        // Enter local monitor. Return true if entered.
        bool enter_local_monitor_for_cert(TrxHandleMaster*,
                                          const TrxHandleSlavePtr&);
        wsrep_status_t handle_local_monitor_interrupted(TrxHandleMaster*,
                                                        const TrxHandleSlavePtr&);
        wsrep_status_t finish_cert(TrxHandleMaster*,
                                   const TrxHandleSlavePtr&);
        wsrep_status_t cert             (TrxHandleMaster*,
                                         const TrxHandleSlavePtr&);
        wsrep_status_t cert_and_catch   (TrxHandleMaster*,
                                         const TrxHandleSlavePtr&);
        wsrep_status_t cert_for_aborted (const TrxHandleSlavePtr&);

        // Enter apply monitor for local transaction. Return true
        // if apply monitor was grabbed.
        bool enter_apply_monitor_for_local(TrxHandleMaster&,
                                           const TrxHandleSlavePtr&);
        wsrep_status_t handle_apply_monitor_interrupted(TrxHandleMaster&,
                                                        const TrxHandleSlavePtr&);
        void enter_apply_monitor_for_local_not_committing(
            const TrxHandleMaster&,
            TrxHandleSlave&);
        wsrep_status_t handle_commit_interrupt(TrxHandleMaster&,
                                               const TrxHandleSlave&);

        void update_state_uuid    (const wsrep_uuid_t& u);
        void update_incoming_list (const wsrep_view_info_t& v);

        /* aborts/exits the program in a clean way */
        void abort() GU_NORETURN;

#ifdef GALERA_MONITOR_DEBUG_PRINT
    public:
#endif /* GALERA_MONITOR_DEBUG_PRINT */

        class LocalOrder
        {
        public:

            explicit
            LocalOrder(const TrxHandleSlave& ts)
                :
                seqno_(ts.local_seqno())
#if defined(GU_DBUG_ON) || !defined(NDEBUG)
                ,trx_(&ts)
#endif //GU_DBUG_ON
            { }

            LocalOrder(wsrep_seqno_t seqno, const TrxHandleSlave* ts = NULL)
                :
                seqno_(seqno)
#if defined(GU_DBUG_ON) || !defined(NDEBUG)
                ,trx_(ts)
#endif //GU_DBUG_ON
            {
#if defined(GU_DBUG_ON) || !defined(NDEBUG)
                assert((trx_ && seqno_ == trx_->local_seqno()) || !trx_);
#endif //GU_DBUG_ON
            }

            wsrep_seqno_t seqno() const { return seqno_; }

            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                return (last_left + 1 == seqno_);
            }

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (trx_)
                {
                    if (trx_->local())
                    {
                        mutex.unlock();
                        GU_DBUG_SYNC_WAIT("local_monitor_master_enter_sync");
                        mutex.lock();
                    }
                    else
                    {
                        mutex.unlock();
                        GU_DBUG_SYNC_WAIT("local_monitor_slave_enter_sync");
                        mutex.lock();
                    }
                }
            }
#endif //GU_DBUG_ON

#ifndef NDEBUG
            LocalOrder()
                :
                seqno_(WSREP_SEQNO_UNDEFINED)
#if defined(GU_DBUG_ON) || !defined(NDEBUG)
                ,trx_(NULL)
#endif /* GU_DBUG_ON || !NDEBUG */
            {}
#endif /* NDEBUG */

            void print(std::ostream& os) const
            {
                os << seqno_;
            }

        private:
#ifdef NDEBUG
            LocalOrder(const LocalOrder& o);
#endif /* NDEBUG */
            wsrep_seqno_t const seqno_;
#if defined(GU_DBUG_ON) || !defined(NDEBUG)
            // this pointer is for debugging purposes only and
            // is not guaranteed to point at a valid location
            const TrxHandleSlave* const trx_;
#endif /* GU_DBUG_ON || !NDEBUG */
        };

        class ApplyOrder
        {
        public:

            ApplyOrder(const TrxHandleSlave& ts)
                :
                global_seqno_ (ts.global_seqno()),
                depends_seqno_(ts.depends_seqno()),
                is_local_     (ts.local()),
                is_toi_       (ts.is_toi())
#ifndef NDEBUG
                ,trx_         (&ts)
#endif
            {
#ifndef NDEBUG
                (void)trx_; // to pacify clang's -Wunused-private-field
#endif
            }

            ApplyOrder(wsrep_seqno_t gs,
                       wsrep_seqno_t ds,
                       bool          l = false)
                :
                global_seqno_ (gs),
                depends_seqno_(ds),
                is_local_     (l),
                is_toi_       (false)
#ifndef NDEBUG
                ,trx_         (NULL)
#endif
            { }

            wsrep_seqno_t seqno() const { return global_seqno_; }

            bool condition(wsrep_seqno_t last_entered,
                           wsrep_seqno_t last_left) const
            {
                return ((is_local_ == true && is_toi_ == false) ||
                        last_left >= depends_seqno_);
            }

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (is_local_)
                {
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("apply_monitor_master_enter_sync");
                    mutex.lock();
                }
                else
                {
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("apply_monitor_slave_enter_sync");
                    mutex.lock();
                }
            }
#endif //GU_DBUG_ON

#ifndef NDEBUG
            ApplyOrder()
                :
                global_seqno_ (WSREP_SEQNO_UNDEFINED),
                depends_seqno_(WSREP_SEQNO_UNDEFINED),
                is_local_     (false),
                is_toi_       (false),
                trx_          (NULL)
            {}
#endif /* NDEBUG */

            void print(std::ostream& os) const
            {
                os << "g:" << global_seqno_
                   << " d:" << depends_seqno_
                   << (is_local_ ? " L" : " R");
            }

        private:
#ifdef NDEBUG
            ApplyOrder(const ApplyOrder&);
#endif /* NDEBUG */
            const wsrep_seqno_t global_seqno_;
            const wsrep_seqno_t depends_seqno_;
            const bool is_local_;
            const bool is_toi_;
#ifndef NDEBUG
            // this pointer is for debugging purposes only and
            // is not guaranteed to point at a valid location
            const TrxHandleSlave* const trx_;
#endif
        };

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

            CommitOrder(const TrxHandleSlave& ts, Mode mode)
                :
                global_seqno_(ts.global_seqno()),
                mode_(mode),
                is_local_(ts.local())
#ifndef NDEBUG
                ,trx_(&ts)
#endif
            { }

            CommitOrder(wsrep_seqno_t gs, Mode mode, bool local = false)
                :
                global_seqno_(gs),
                mode_(mode),
                is_local_(local)
#ifndef NDEBUG
                ,trx_(NULL)
#endif
            { }

            wsrep_seqno_t seqno() const { return global_seqno_; }

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
                    return is_local_;
                    // in case of remote trx fall through
                case NO_OOOC:
                    return (last_left + 1 == global_seqno_);
                }
                gu_throw_fatal << "invalid commit mode value " << mode_;
            }

#ifdef GU_DBUG_ON
            void debug_sync(gu::Mutex& mutex)
            {
                if (is_local_ == true)
                {
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("commit_monitor_master_enter_sync");
                    mutex.lock();
                }
                else
                {
                    mutex.unlock();
                    GU_DBUG_SYNC_WAIT("commit_monitor_slave_enter_sync");
                    mutex.lock();
                }
            }
#endif //GU_DBUG_ON

#ifndef NDEBUG
            CommitOrder()
                :
                global_seqno_ (WSREP_SEQNO_UNDEFINED),
                mode_         (OOOC),
                is_local_     (false),
                trx_          (NULL)
            {
                (void)trx_; // to pacify clang's -Wunused-private-field
            }
#endif /* NDEBUG */

            void print(std::ostream& os) const
            {
                os << "g:" << global_seqno_ << " m:" << mode_
                   << (is_local_ ? " L" : " R");
            }

        private:
#ifdef NDEBUG
            CommitOrder(const CommitOrder&);
#endif
            const wsrep_seqno_t global_seqno_;
            const Mode mode_;
            const bool is_local_;
#ifndef NDEBUG
            // this pointer is for debugging purposes only and
            // is not guaranteed to point at a valid location
            const TrxHandleSlave* const trx_;
#endif
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

        void cancel_seqno(wsrep_seqno_t);

        void set_initial_position(const wsrep_uuid_t&, wsrep_seqno_t);

        void establish_protocol_versions (int version);

        /*
         * Record cc_seqno_ and cc_lowest_trx_seqno_ for future IST
         * processing.
         *
         * @param cc_seqno Seqno of current configuration change.
         * @param source String describing the source of the configuration
         *               change.
         */
        void record_cc_seqnos(wsrep_seqno_t cc_seqno, const char* source);

        bool state_transfer_required(const wsrep_view_info_t& view_info,
                                     int group_proto_ver,
                                     bool rejoined);

        void prepare_for_IST (void*& req, ssize_t& req_len,
                              int group_proto_ver,
                              int str_proto_ver,
                              const wsrep_uuid_t& group_uuid,
                              wsrep_seqno_t       group_seqno);

        void recv_IST(void* recv_ctx);
        void process_IST_writeset(void* recv_ctx, const TrxHandleSlavePtr& ts);

        StateRequest* prepare_state_request (const void* sst_req,
                                             ssize_t     sst_req_len,
                                             int         group_proto_ver,
                                             int         str_proto_ver,
                                             const wsrep_uuid_t& group_uuid,
                                             wsrep_seqno_t       group_seqno);

        void send_state_request (const StateRequest* req, int str_proto_ver);

        void request_state_transfer (void* recv_ctx,
                                     int                 group_proto_ver,
                                     const wsrep_uuid_t& group_uuid,
                                     wsrep_seqno_t       group_seqno,
                                     const void*         sst_req,
                                     ssize_t             sst_req_len);

        /* resume reception of GCS events */
        void resume_recv() { gcs_.resume_recv(); ist_end(0); }

        /* These methods facilitate closing procedure.
         * They must be called under closing_mutex_ lock */
        void start_closing();
        void shift_to_CLOSED();
        void wait_for_CLOSED(gu::Lock&);

        wsrep_seqno_t donate_sst(void* recv_ctx, const StateRequest& streq,
                                 const wsrep_gtid_t& state_id, bool bypass);

        /* Wait until NBO end criteria is met */
        wsrep_status_t wait_nbo_end(TrxHandleMaster*, wsrep_trx_meta_t*);

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
         * |--------------------------------------------------------------------|
         * | protocol_version_ | trx version | str_proto_ver  | record_set_ver_ |
         * |--------------------------------------------------------------------|
         * |                 1 |           1 |              0 |               1 |
         * |                 2 |           1 |              1 |               1 |
         * |                 3 |           2 |              1 |               1 |
         * |                 4 |           2 |  v2.1        1 |               1 |
         * |                 5 |           3 |              1 |               1 |
         * |                 6 |           3 |              2 |               1 |
         * |                 7 |           3 |              2 |               1 |
         * |                 8 |           3 |              2 | alignment     2 |
         * |                 9 | SS keys   4 |              2 |               2 |
         * | 4.x            10 | PA range/ 5 | CC events /  3 |               2 |
         * |                   | UPD keys    | idx preload    |                 |
         * |--------------------------------------------------------------------|
         *
         * Note: str_proto_ver is decided in replicator_str.cpp based on
         *       given protocol version.
         */

        /* last protocol version of Galera 3 series */
        static int const PROTO_VER_GALERA_3_MAX = 9;
        /* repl protocol version which orders CC */
        static int const PROTO_VER_ORDERED_CC = 10;

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

        // boolean telling if the node is safe to use for bootstrapping
        // a new primary component
        bool safe_to_bootstrap_;

        // currently installed trx parameters
        TrxHandleMaster::Params trx_params_;

        // identifiers
        wsrep_uuid_t          uuid_;
        wsrep_uuid_t const    state_uuid_;
        const char            state_uuid_str_[37];
        wsrep_seqno_t         cc_seqno_; // seqno of last CC
        // Lowest trx seqno in cert index during last CC
        wsrep_seqno_t         cc_lowest_trx_seqno_;
        wsrep_seqno_t         pause_seqno_; // local seqno of last pause call

        // application callbacks
        void*                  app_ctx_;
        wsrep_connected_cb_t   connected_cb_;
        wsrep_view_cb_t        view_cb_;
        wsrep_sst_request_cb_t sst_request_cb_;
        wsrep_apply_cb_t       apply_cb_;
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
        ist::Receiver        ist_receiver_;
        ist::AsyncSenderMap  ist_senders_;

        // trx processing
        Wsdb            wsdb_;
        Certification   cert_;

        class PendingCertQueue
        {
        public:
            PendingCertQueue(gcache::GCache& gcache) :
                mutex_(),
                ts_queue_(),
                gcache_(gcache)
            { }

            void push(const TrxHandleSlavePtr& ts)
            {
                assert(ts->local());
                assert(ts->local_seqno() > 0);
                gu::Lock lock(mutex_);
                ts_queue_.push(ts);
                ts->mark_queued();
            }

            TrxHandleSlavePtr must_cert_next(wsrep_seqno_t seqno)
            {
                gu::Lock lock(mutex_);
                TrxHandleSlavePtr ret;
                if (!ts_queue_.empty())
                {
                    const TrxHandleSlavePtr& top(ts_queue_.top());
                    assert(top->local_seqno() != seqno);
                    if (top->local_seqno() < seqno)
                    {
                        ret = top;
                        ts_queue_.pop();
                    }
                }
                return ret;
            }

            void clear()
            {
                gu::Lock lock(mutex_);
                while (not ts_queue_.empty())
                {
                    TrxHandleSlavePtr ts(ts_queue_.top());
                    ts_queue_.pop();
                    gcache_.free(const_cast<void*>(ts->action().first));
                }
            }

        private:
            struct TrxHandleSlavePtrCmpLocalSeqno
            {
                bool operator()(const TrxHandleSlavePtr& lhs,
                                const TrxHandleSlavePtr& rhs) const
                {
                    return lhs->local_seqno() > rhs->local_seqno();
                }
            };
            gu::Mutex mutex_;
            std::priority_queue<TrxHandleSlavePtr,
                                std::vector<TrxHandleSlavePtr>,
                                TrxHandleSlavePtrCmpLocalSeqno> ts_queue_;
            gcache::GCache& gcache_;
        };

        PendingCertQueue pending_cert_queue_;

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

#ifdef GALERA_MONITOR_DEBUG_PRINT
    inline std::ostream&
    operator<<(std::ostream& os,const ReplicatorSMM::LocalOrder& o)
    { o.print(os); return os; }
    inline std::ostream&
    operator<<(std::ostream& os,const ReplicatorSMM::ApplyOrder& o)
    { o.print(os); return os; }
    inline std::ostream&
    operator<<(std::ostream& os,const ReplicatorSMM::CommitOrder& o)
    { o.print(os); return os; }
#endif /* GALERA_MONITOR_DEBUG_PRINT */

    /**
     * Get transaction protocol and record set versions based on group protocol
     *
     * @param proto_ver Group protocol version
     *
     * @return Tuple consisting of trx protocol and record set versions.
     */
    std::tuple<int, enum gu::RecordSet::Version>
    get_trx_protocol_versions(int proto_ver);
} /* namespace galera */

#endif /* GALERA_REPLICATOR_SMM_HPP */
