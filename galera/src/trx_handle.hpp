//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//


#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "fsm.hpp"
#include "key_data.hpp" // for append_key()
#include "key_entry_os.hpp"
#include "write_set_ng.hpp"

#include "wsrep_api.h"
#include "gu_mutex.hpp"
#include "gu_atomic.hpp"
#include "gu_datetime.hpp"
#include "gu_unordered.hpp"
#include "gu_utils.hpp"
#include "gu_macros.hpp"
#include "gu_mem_pool.hpp"
#include "gu_vector.hpp"
#include "gu_shared_ptr.hpp"
#include "gcs.hpp"
#include "gu_limits.h" // page size stuff

#include <set>

namespace galera
{

    class NBOCtx; // forward decl

    static std::string const working_dir = "/tmp";

    // Helper template for building FSMs.
    template <typename T>
    class TransMapBuilder
    {
    public:

        TransMapBuilder() { }

        void add(typename T::State from, typename T::State to)
        {
            trans_map_.insert_unique(
                std::make_pair(typename T::Transition(from, to),
                               typename T::Fsm::TransAttr()));
        }
    private:
        typename T::Fsm::TransMap& trans_map_;
    };


    class TrxHandle
    {
    public:

        enum Flags
        {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_ISOLATION   = 1 << 2,
            F_PA_UNSAFE   = 1 << 3,
            F_COMMUTATIVE = 1 << 4,
            F_NATIVE      = 1 << 5,
            F_BEGIN       = 1 << 6,
            F_PREPARE     = 1 << 7,
            F_SNAPSHOT    = 1 << 8,
            F_IMPLICIT_DEPS = 1 << 9,
            /*
             * reserved for API extension
             */
            F_PREORDERED  = 1 << 15 // flag specific to WriteSet
            /*
             * reserved for internal use
             */
        };

        static const uint32_t TRXHANDLE_FLAGS_MASK = (1 << 15) | ((1 << 10) - 1);
        static const uint32_t EXPLICIT_ROLLBACK_FLAGS = F_PA_UNSAFE | F_ROLLBACK;

        static bool const FLAGS_MATCH_API_FLAGS =
                                 (WSREP_FLAG_TRX_END     == F_COMMIT       &&
                                  WSREP_FLAG_ROLLBACK    == F_ROLLBACK     &&
                                  WSREP_FLAG_ISOLATION   == F_ISOLATION    &&
                                  WSREP_FLAG_PA_UNSAFE   == F_PA_UNSAFE    &&
                                  WSREP_FLAG_COMMUTATIVE == F_COMMUTATIVE  &&
                                  WSREP_FLAG_NATIVE      == F_NATIVE       &&
                                  WSREP_FLAG_TRX_START   == F_BEGIN        &&
                                  WSREP_FLAG_TRX_PREPARE == F_PREPARE      &&
                                  WSREP_FLAG_SNAPSHOT    == F_SNAPSHOT     &&
                                  WSREP_FLAG_IMPLICIT_DEPS == F_IMPLICIT_DEPS &&
                                  int(WriteSetNG::F_PREORDERED) ==F_PREORDERED);

        static uint32_t wsrep_flags_to_trx_flags (uint32_t flags);
        static uint32_t trx_flags_to_wsrep_flags (uint32_t flags);
        static uint32_t ws_flags_to_trx_flags    (uint32_t flags);

        bool is_toi() const
        {
            return ((write_set_flags_ & F_ISOLATION) != 0);
        }

        bool pa_unsafe() const
        {
            return ((write_set_flags_ & F_PA_UNSAFE) != 0);
        }

        bool preordered() const
        {
            return ((write_set_flags_ & F_PREORDERED) != 0);
        }

        bool nbo_start() const
        {
            return (is_toi() &&
                    (write_set_flags_ & F_BEGIN) != 0 &&
                    (write_set_flags_ & F_COMMIT) == 0);
        }

        bool nbo_end() const
        {
            return (is_toi() &&
                    (write_set_flags_ & F_BEGIN) == 0 &&
                    (write_set_flags_ & F_COMMIT) != 0);
        }

        typedef enum
        {
            S_EXECUTING,
            S_MUST_ABORT,
            S_ABORTING,
            S_REPLICATING,
            S_CERTIFYING,
            S_MUST_REPLAY,    // replay
            S_REPLAYING,
            S_APPLYING,   // grabbing apply monitor, applying
            S_COMMITTING, // grabbing commit monitor, committing changes
            S_ROLLING_BACK,
            S_COMMITTED,
            S_ROLLED_BACK
        } State;

        static const int num_states_ = S_ROLLED_BACK + 1;

        static void print_state(std::ostream&, State);

        void print_state_history(std::ostream&) const;

        class Transition
        {
        public:

            Transition(State const from, State const to) : from_(from), to_(to)
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
                    return (gu::HashValue(static_cast<int>(tr.from_)) ^
                            gu::HashValue(static_cast<int>(tr.to_)));
                }
            };

        private:

            State from_;
            State to_;
        }; // class Transition

        typedef FSM<State, Transition> Fsm;

        int  version()     const { return version_; }

        const wsrep_uuid_t& source_id() const { return source_id_; }
        wsrep_trx_id_t      trx_id()    const { return trx_id_;    }

        void set_local(bool local) { local_ = local; }
        bool local() const { return local_; }

        wsrep_conn_id_t conn_id() const { return conn_id_;   }
        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }

        State state() const { return state_(); }

        void print_set_state(State state) const;

        uint32_t flags() const { return write_set_flags_; }
        void set_flags(uint32_t flags) { write_set_flags_ = flags; }

        uint64_t timestamp() const { return timestamp_; }

        bool master() const { return master_; }

        void print(std::ostream& os) const;

        virtual ~TrxHandle() {}

        // Force state, for testing purposes only.
        void force_state(State state)
        {
            state_.force(state);
        }

    protected:

        void  set_state(State const state, int const line)
        {
            state_.shift_to(state, line);
            if (state == S_EXECUTING) state_.reset_history();
        }

        /* slave trx ctor */
        TrxHandle(Fsm::TransMap* trans_map, bool local)
            :
            state_             (trans_map, S_REPLICATING),
            source_id_         (WSREP_UUID_UNDEFINED),
            conn_id_           (-1),
            trx_id_            (-1),
            timestamp_         (),
            version_           (-1),
            write_set_flags_   (0),
            local_             (local),
            master_            (false)
        {}

        /* local trx ctor */
        TrxHandle(Fsm::TransMap*      trans_map,
                  const wsrep_uuid_t& source_id,
                  wsrep_conn_id_t     conn_id,
                  wsrep_trx_id_t      trx_id,
                  int                 version)
            :
            state_             (trans_map, S_EXECUTING),
            source_id_         (source_id),
            conn_id_           (conn_id),
            trx_id_            (trx_id),
            timestamp_         (gu_time_calendar()),
            version_           (version),
            write_set_flags_   (F_BEGIN),
            local_             (true),
            master_            (true)
        {}

        Fsm state_;
        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        int64_t                timestamp_;
        int                    version_;
        uint32_t               write_set_flags_;
        // Boolean denoting if the TrxHandle was generated locally.
        // Always true for TrxHandleMaster, set to true to
        // TrxHandleSlave if there exists TrxHandleMaster object corresponding
        // to TrxHandleSlave.
        bool                   local_;
        bool                   master_; // derived object type

    private:

        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        friend class Wsdb;
        friend class Certification;

        template <bool>
        static inline uint32_t wsrep_flags_to_trx_flags_tmpl (uint32_t flags)
        {
            assert(0); // remove when needed
            uint32_t ret(0);

            if (flags & WSREP_FLAG_TRX_END)     ret |= F_COMMIT;
            if (flags & WSREP_FLAG_ROLLBACK)    ret |= F_ROLLBACK;
            if (flags & WSREP_FLAG_ISOLATION)   ret |= F_ISOLATION;
            if (flags & WSREP_FLAG_PA_UNSAFE)   ret |= F_PA_UNSAFE;
            if (flags & WSREP_FLAG_COMMUTATIVE) ret |= F_COMMUTATIVE;
            if (flags & WSREP_FLAG_NATIVE)      ret |= F_NATIVE;
            if (flags & WSREP_FLAG_TRX_START)   ret |= F_BEGIN;
            if (flags & WSREP_FLAG_TRX_PREPARE) ret |= F_PREPARE;

            return ret;
        }

        template <bool>
        static inline uint32_t trx_flags_to_wsrep_flags_tmpl (uint32_t flags)
        {
            assert(0); // remove when needed
            uint32_t ret(0);

            if (flags & F_COMMIT)      ret |= WSREP_FLAG_TRX_END;
            if (flags & F_ROLLBACK)    ret |= WSREP_FLAG_ROLLBACK;
            if (flags & F_ISOLATION)   ret |= WSREP_FLAG_ISOLATION;
            if (flags & F_PA_UNSAFE)   ret |= WSREP_FLAG_PA_UNSAFE;
            if (flags & F_COMMUTATIVE) ret |= WSREP_FLAG_COMMUTATIVE;
            if (flags & F_NATIVE)      ret |= WSREP_FLAG_NATIVE;
            if (flags & F_BEGIN)       ret |= WSREP_FLAG_TRX_START;
            if (flags & F_PREPARE)     ret |= WSREP_FLAG_TRX_PREPARE;

            return ret;
        }

        template <bool>
        static inline uint32_t ws_flags_to_trx_flags_tmpl (uint32_t flags)
        {
            assert(0); // remove when needed
            uint32_t ret(0);

            if (flags & WriteSetNG::F_COMMIT)      ret |= F_COMMIT;
            if (flags & WriteSetNG::F_ROLLBACK)    ret |= F_ROLLBACK;
            if (flags & WriteSetNG::F_TOI)         ret |= F_ISOLATION;
            if (flags & WriteSetNG::F_PA_UNSAFE)   ret |= F_PA_UNSAFE;
            if (flags & WriteSetNG::F_COMMUTATIVE) ret |= F_COMMUTATIVE;
            if (flags & WriteSetNG::F_NATIVE)      ret |= F_NATIVE;
            if (flags & WriteSetNG::F_BEGIN)       ret |= F_BEGIN;
            if (flags & WriteSetNG::F_PREORDERED)  ret |= F_PREORDERED;
            if (flags & WriteSetNG::F_PREPARE)     ret |= F_PREPARE;

            return ret;
        }

    }; /* class TrxHandle */

    template <> inline uint32_t
    TrxHandle::wsrep_flags_to_trx_flags_tmpl<true>(uint32_t const flags)
    { return flags; }

    inline uint32_t
    TrxHandle::wsrep_flags_to_trx_flags (uint32_t const flags)
    { return wsrep_flags_to_trx_flags_tmpl<FLAGS_MATCH_API_FLAGS>(flags); }

    template <> inline uint32_t
    TrxHandle::trx_flags_to_wsrep_flags_tmpl<true>(uint32_t flags)
    { return (flags & WSREP_FLAGS_MASK); }

    inline uint32_t
    TrxHandle::trx_flags_to_wsrep_flags (uint32_t const flags)
    { return trx_flags_to_wsrep_flags_tmpl<FLAGS_MATCH_API_FLAGS>(flags); }

    template <> inline uint32_t
    TrxHandle::ws_flags_to_trx_flags_tmpl<true>(uint32_t flags)
    { return (flags & TRXHANDLE_FLAGS_MASK); }

    inline uint32_t
    TrxHandle::ws_flags_to_trx_flags (uint32_t const flags)
    { return ws_flags_to_trx_flags_tmpl<FLAGS_MATCH_API_FLAGS>(flags); }

    std::ostream& operator<<(std::ostream& os, TrxHandle::State s);
    std::ostream& operator<<(std::ostream& os, const TrxHandle& trx);

    class TrxHandleSlave;
    std::ostream& operator<<(std::ostream& os, const TrxHandleSlave& th);

    class TrxHandleSlave : public TrxHandle
    {
    public:

        typedef gu::MemPool<true> Pool;
        static TrxHandleSlave* New(bool local, Pool& pool)
        {
            assert(pool.buf_size() == sizeof(TrxHandleSlave));

            void* const buf(pool.acquire());

            return new(buf) TrxHandleSlave(local, pool, buf);
        }

        /**
         * Adjust flags for backwards compatibility.
         *
         * Galera 4.x assigns some write set flags differently from
         * 3.x. During rolling upgrade these changes need to be
         * taken into account as 3.x originated write sets may not
         * have all flags set which are required for replicator internal
         * operation. The adjustment is done here in order to avoid spreading
         * the protocol specific changes up to stack.
         *
         * In particular the lack of F_BEGIN flag in 3.x needs to be
         * take care of.
         *
         * F_BEGIN - All of the write sets which originate from 3.x
         *           (version < VER5) which have F_COMMIT flag set
         *           must be assigned also F_BEGIN for internal operation.
         *           This is safe because 3.x does not have SR or NBO
         *           implemented, all transactions and TOI write sets
         *           are self contained.
         *
         * @param version Write Set wire version
         * @param flags Flags from write set
         *
         * @return Adjusted write set flags compatible with current
         *         implementation.
         */
        static inline uint32_t
        fixup_write_set_flags(int version, uint32_t flags)
        {
            if (version < WriteSetNG::VER5)
            {
                if (flags & F_COMMIT)
                {
                    flags |= F_BEGIN;
                }
            }
            return flags;
        }

        template <bool from_group>
        size_t unserialize(const gcs_action& act)
        {
            assert(GCS_ACT_WRITESET == act.type);

            try
            {
                version_ = WriteSetNG::version(act.buf, act.size);
                action_  = std::make_pair(act.buf, act.size);

                switch (version_)
                {
                case WriteSetNG::VER3:
                case WriteSetNG::VER4:
                case WriteSetNG::VER5:
                    write_set_.read_buf (act.buf, act.size);
                    assert(version_ == write_set_.version());
                    write_set_flags_ = fixup_write_set_flags(
                        version_,
                        ws_flags_to_trx_flags(write_set_.flags()));
                    source_id_       = write_set_.source_id();
                    conn_id_         = write_set_.conn_id();
                    trx_id_          = write_set_.trx_id();
#ifndef NDEBUG
                    write_set_.verify_checksum();

                    assert(source_id_ != WSREP_UUID_UNDEFINED);
                    assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
                    assert(WSREP_SEQNO_UNDEFINED == local_seqno_);
                    assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
#endif
                    if (from_group)
                    {
                        local_seqno_     = act.seqno_l;
                        global_seqno_    = act.seqno_g;

                        if (write_set_flags_ & F_PREORDERED)
                        {
                            last_seen_seqno_ = global_seqno_ - 1;
                        }
                        else
                        {
                            last_seen_seqno_ = write_set_.last_seen();
                        }
#ifndef NDEBUG
                        assert(last_seen_seqno_ >= 0);
                        if (last_seen_seqno_ >= global_seqno_)
                        {
                            log_fatal << "S: global: "   << global_seqno_
                                      << ", last_seen: " << last_seen_seqno_
                                      << ", checksum: "  <<
                                gu::PrintBase<>(write_set_.get_checksum());
                        }
                        assert(last_seen_seqno_ < global_seqno_);
#endif
                        if (gu_likely(0 ==
                                      (flags() & (TrxHandle::F_ISOLATION |
                                                  TrxHandle::F_PA_UNSAFE))))
                        {
                            assert(WSREP_SEQNO_UNDEFINED == depends_seqno_);

                            if (gu_likely(version_) >= WriteSetNG::VER5)
                            {
                                depends_seqno_ = std::max<wsrep_seqno_t>
                                    (last_seen_seqno_ - write_set_.pa_range(),
                                     WSREP_SEQNO_UNDEFINED);
                            }

                            /* just in case Galera 3.x uses this don't
                               condition it on version_ */
                            if (flags() & F_IMPLICIT_DEPS)
                            {
                                assert(last_seen_seqno_ >= depends_seqno_);
                                depends_seqno_ = last_seen_seqno_;
                            }
                        }
                        else
                        {
                            depends_seqno_ = global_seqno_ - 1;
                        }
                    }
                    else
                    {
                        assert(!local_);

                        global_seqno_  = write_set_.seqno();
                        if (gu_likely(!(nbo_end())))
                        {
                            depends_seqno_ = global_seqno_-write_set_.pa_range();
                            assert(depends_seqno_ >= 0);
                        }
                        assert(depends_seqno_ < global_seqno_);
                        certified_ = true;
                    }
#ifndef NDEBUG
                    explicit_rollback_ =
                        (write_set_flags_ == EXPLICIT_ROLLBACK_FLAGS);
#endif /* NDEBUG */
                    timestamp_ = write_set_.timestamp();

                    assert(trx_id() != uint64_t(-1) || is_toi());
                    sanity_checks();

                    break;
                default:
                    gu_throw_error(EPROTONOSUPPORT) <<"Unsupported WS version: "
                                                    << version_;
                }

                return act.size;
            }
            catch (gu::Exception& e)
            {
                GU_TRACE(e);
                deserialize_error_log(e);
                throw;
            }
        }

        void verify_checksum() const /* throws */
        {
            write_set_.verify_checksum();
        }

        void update_stats(gu::Atomic<long long>& kc,
                          gu::Atomic<long long>& kb,
                          gu::Atomic<long long>& db,
                          gu::Atomic<long long>& ub)
        {
            kc += write_set_.keyset().count();
            kb += write_set_.keyset().size();
            db += write_set_.dataset().size();
            ub += write_set_.unrdset().size();
        }

        bool certified() const { return certified_; }

        void mark_certified()
        {
            assert(!certified_);

            int dw(0);

            if (gu_likely(depends_seqno_ >= 0))
            {
                dw = global_seqno_ - depends_seqno_;
            }

            /* make sure to not exceed original pa_range() */
            assert(version_ < WriteSetNG::VER5 ||
                   last_seen_seqno_ - write_set_.pa_range() <=
                   global_seqno_ - dw || preordered());

            write_set_.set_seqno(global_seqno_, dw);

            certified_ = true;
        }

        void set_depends_seqno(wsrep_seqno_t const seqno_lt)
        {
            /* make sure depends_seqno_ never goes down */
            assert(seqno_lt >= depends_seqno_ ||
                   seqno_lt == WSREP_SEQNO_UNDEFINED ||
                   preordered());
            depends_seqno_ = seqno_lt;
        }

        void set_global_seqno(wsrep_seqno_t s) // for monitor cancellation
        {
            global_seqno_ = s;
        }

        void set_state(TrxHandle::State const state, int const line = -1)
        {
            TrxHandle::set_state(state, line);
        }

        void apply(void*                   recv_ctx,
                   wsrep_apply_cb_t        apply_cb,
                   const wsrep_trx_meta_t& meta,
                   wsrep_bool_t&           exit_loop) /* throws */;

        bool is_committed() const { return committed_; }
        void mark_committed()     { committed_ = true; }

        void unordered(void*                recv_ctx,
                       wsrep_unordered_cb_t apply_cb) const;

        std::pair<const void*, size_t> action() const
        {
            return action_;
        }

        wsrep_seqno_t local_seqno()     const { return local_seqno_; }

        wsrep_seqno_t global_seqno()    const { return global_seqno_; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t depends_seqno()   const { return depends_seqno_; }

        const WriteSetIn&  write_set () const { return write_set_;  }

        bool   exit_loop() const { return exit_loop_; }
        void   set_exit_loop(bool x) { exit_loop_ |= x; }

        typedef gu::UnorderedMap<KeyEntryOS*,
                                 std::pair<bool, bool>,
                                 KeyEntryPtrHash,
                                 KeyEntryPtrEqualAll> CertKeySet;

        void print(std::ostream& os) const;

        uint64_t get_checksum() const { return write_set_.get_checksum(); }

        size_t   size()         const { return write_set_.size(); }

        void set_ends_nbo(wsrep_seqno_t seqno) { ends_nbo_ = seqno; }
        wsrep_seqno_t ends_nbo() const { return ends_nbo_; }

        void mark_dummy()
        {
            set_depends_seqno(WSREP_SEQNO_UNDEFINED);
            set_flags(flags() | F_ROLLBACK);
        }

        // Mark action dummy and assign gcache buffer pointer. The
        // action size is left zero.
        void mark_dummy_with_action(const void* buf)
        {
            mark_dummy();
            action_.first = buf;
            action_.second = 0;
        }
        bool is_dummy() const
        {
            return (flags() &  F_ROLLBACK) &&
                (flags() != EXPLICIT_ROLLBACK_FLAGS);
        }
        bool skip_event() const { return (flags() == F_ROLLBACK); }

        bool is_streaming() const
        {
            return !((flags() & F_BEGIN) && (flags() & F_COMMIT));
        }

        void cert_bypass(bool const val)
        {
            assert(true  == val);
            assert(false == cert_bypass_);
            cert_bypass_ = val;
        }
        bool cert_bypass() const { return cert_bypass_; }

        bool explicit_rollback() const
        {
            bool const ret(flags() == EXPLICIT_ROLLBACK_FLAGS);
            assert(ret == explicit_rollback_);
            return ret;
        }

        void mark_queued()
        {
            assert(!queued_);
            queued_ = true;
        }
        bool queued() const { return queued_; }

    protected:

        TrxHandleSlave(bool local, gu::MemPool<true>& mp, void* buf) :
            TrxHandle          (&trans_map_, local),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            ends_nbo_          (WSREP_SEQNO_UNDEFINED),
            mem_pool_          (mp),
            write_set_         (),
            buf_               (buf),
            action_            (static_cast<const void*>(0), 0),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false),
            cert_bypass_       (false),
            queued_            (false)
#ifndef NDEBUG
            ,explicit_rollback_(false)
#endif /* NDEBUG */
        {}

        friend class TrxHandleMaster;
        friend class TransMapBuilder<TrxHandleSlave>;
        friend class TrxHandleSlaveDeleter;

    private:
        static Fsm::TransMap trans_map_;

        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          depends_seqno_;
        wsrep_seqno_t          ends_nbo_;
        gu::MemPool<true>&     mem_pool_;
        WriteSetIn             write_set_;
        void* const            buf_;
        std::pair<const void*, size_t> action_;
        bool                   certified_;
        bool                   committed_;
        bool                   exit_loop_;
        bool                   cert_bypass_;
        bool                   queued_;
#ifndef NDEBUG
        bool                   explicit_rollback_;
#endif /* NDEBUG */

        TrxHandleSlave(const TrxHandleSlave&);
        void operator=(const TrxHandleSlave& other);

        ~TrxHandleSlave()
        {
#ifndef NDEBUG
            if (explicit_rollback_) assert (flags() == EXPLICIT_ROLLBACK_FLAGS);
#endif /* NDEBUG */
        }

        void destroy_local(void* ptr);

        void sanity_checks() const;

        void deserialize_error_log(const gu::Exception& e) const;

    }; /* TrxHandleSlave */

    typedef gu::shared_ptr<TrxHandleSlave>::type TrxHandleSlavePtr;

    class TrxHandleSlaveDeleter
    {
    public:
        void operator()(TrxHandleSlave* ptr)
        {
            gu::MemPool<true>& mp(ptr->mem_pool_);
            ptr->~TrxHandleSlave();
            mp.recycle(ptr);
        }
    };

    class TrxHandleMaster : public TrxHandle
    {
    public:
        /* signed int here is to detect SIZE < sizeof(TrxHandle) */
        static size_t LOCAL_STORAGE_SIZE()
        {
            static size_t const ret(gu_page_size_multiple(1 << 13 /* 8Kb */));
            return ret;
        }

        struct Params
        {
            std::string            working_dir_;
            int                    version_;
            KeySet::Version        key_format_;
            gu::RecordSet::Version record_set_ver_;
            int                    max_write_set_size_;

            Params (const std::string& wdir,
                    int                ver,
                    KeySet::Version    kformat,
                    gu::RecordSet::Version rsv = gu::RecordSet::VER2,
                    int                max_write_set_size = WriteSetNG::MAX_SIZE)
                :
                working_dir_       (wdir),
                version_           (ver),
                key_format_        (kformat),
                record_set_ver_    (rsv),
                max_write_set_size_(max_write_set_size)
            {}

            Params () :
                working_dir_(), version_(), key_format_(),
                record_set_ver_(), max_write_set_size_()
            {}
        };

        static const Params Defaults;

        typedef gu::MemPool<true> Pool;
        static TrxHandleMaster* New(Pool&               pool,
                                    const Params&       params,
                                    const wsrep_uuid_t& source_id,
                                    wsrep_conn_id_t     conn_id,
                                    wsrep_trx_id_t      trx_id)
        {
            size_t const buf_size(pool.buf_size());

            assert(buf_size >= (sizeof(TrxHandleMaster) + sizeof(WriteSetOut)));

            void* const buf(pool.acquire());

            return new(buf) TrxHandleMaster(pool, params,
                                            source_id, conn_id, trx_id,
                                            buf_size);
        }

        void lock()
        {
            mutex_.lock();
        }

#ifndef NDEBUG
        bool locked() { return mutex_.locked(); }
        bool owned()  { return mutex_.owned(); }
#endif /* NDEBUG */

        void unlock()
        {
            assert(locked());
            assert(owned());
            mutex_.unlock();
        }

        void set_state(TrxHandle::State const s, int const line = -1)
        {
            assert(locked());
            assert(owned());
            TrxHandle::set_state(s, line);
        }

        long gcs_handle() const { return gcs_handle_; }
        void set_gcs_handle(long gcs_handle) { gcs_handle_ = gcs_handle; }

        void set_flags(uint32_t const flags) // wsrep flags
        {
            TrxHandle::set_flags(flags);

            uint16_t ws_flags(WriteSetNG::wsrep_flags_to_ws_flags(flags));

            write_set_out().set_flags(ws_flags);
        }

        void append_key(const KeyData& key)
        {
            // Current limitations with certification on trx versions 3 to 5
            // impose the the following restrictions on keys

            // The shared key behavior for TOI operations is completely
            // untested, so don't allow it (and it probably does not even
            // make any sense)
            assert(is_toi() == false  || key.shared() == false);

            /*! protection against protocol change during trx lifetime */
            if (key.proto_ver != version())
            {
                gu_throw_error(EINVAL) << "key version '" << key.proto_ver
                                       << "' does not match to trx version' "
                                       << version() << "'";
            }

            gu_trace(write_set_out().append_key(key));
        }

        void append_data(const void* data, const size_t data_len,
                         wsrep_data_type_t type, bool store)
        {
            switch (type)
            {
            case WSREP_DATA_ORDERED:
                gu_trace(write_set_out().append_data(data, data_len, store));
                break;
            case WSREP_DATA_UNORDERED:
                gu_trace(write_set_out().append_unordered(data, data_len,store));
                break;
            case WSREP_DATA_ANNOTATION:
                gu_trace(write_set_out().append_annotation(data,data_len,store));
                break;
            };
        }

        bool empty() const
        {
            return write_set_out().is_empty();
        }

        TrxHandleSlavePtr ts()
        {
            return ts_;
        }

        void reset_ts()
        {
            ts_ = TrxHandleSlavePtr();
        }

        size_t gather(WriteSetNG::GatherVector& out)
        {
            set_ws_flags();
            return write_set_out().gather(source_id(),conn_id(),trx_id(),out);
        }

        void finalize(wsrep_seqno_t last_seen_seqno)
        {
            assert(last_seen_seqno >= 0);
            assert(ts_ == 0 || last_seen_seqno >= ts_->last_seen_seqno());

            int pa_range(pa_range_default());

            if (gu_unlikely((flags() & TrxHandle::F_BEGIN) == 0 &&
                            (flags() & TrxHandle::F_ISOLATION) == 0))
            {
                /* make sure this fragment depends on the previous */
                wsrep_seqno_t prev_seqno(last_ts_seqno_);
                if (prev_seqno == WSREP_SEQNO_UNDEFINED)
                {
                    assert((flags() & TrxHandle::F_COMMIT) ||
                           (flags() & TrxHandle::F_ROLLBACK));
                    prev_seqno = 0;
                }
                assert(version() >= WriteSetNG::VER5);
                assert(prev_seqno >= 0);
                // Although commit happens in order, the release of apply
                // monitor which is used to determine last committed may
                // not happen in order. Therefore it is possible that the
                // last seen given as an argument for this method lies
                // below prev_seqno. Adjust last seen seqno to match
                // at least prev_seqno which is now known to be committed.
                last_seen_seqno = std::max(last_seen_seqno, prev_seqno);
                pa_range = std::min(wsrep_seqno_t(pa_range),
                                    last_seen_seqno - prev_seqno);
            }
            else
            {
                assert(ts_ == 0);
                assert(flags() & TrxHandle::F_ISOLATION ||
                       (flags() & TrxHandle::F_ROLLBACK) == 0);
            }

            write_set_out().finalize(last_seen_seqno, pa_range);
        }

        /* Serializes wiriteset into a single buffer (for unit test purposes) */
        void serialize(wsrep_seqno_t const last_seen,
                       std::vector<gu::byte_t>& ret)
        {
            set_ws_flags();
            write_set_out().serialize(ret, source_id(), conn_id(), trx_id(),
                                      last_seen, pa_range_default());
        }

        void clear()
        {
            release_write_set_out();
        }

        void add_replicated(TrxHandleSlavePtr ts)
        {
            assert(locked());
            if ((write_set_flags_ & TrxHandle::F_ISOLATION) == 0)
            {
                write_set_flags_ &= ~TrxHandle::F_BEGIN;
                write_set_flags_ &= ~TrxHandle::F_PREPARE;
            }
            ts_ = ts;
            last_ts_seqno_ = ts_->global_seqno();
        }

        WriteSetOut& write_set_out()
        {
            /* WriteSetOut is a temporary object needed only at the writeset
             * collection stage. Since it may allocate considerable resources
             * we dont't want it to linger as long as TrxHandle is needed and
             * want to destroy it ASAP. So it is constructed in the buffer
             * allocated by TrxHandle::New() immediately following this object */
            if (gu_unlikely(!wso_)) init_write_set_out();
            assert(wso_);
            return *static_cast<WriteSetOut*>(wso_buf());
        }

        void release_write_set_out()
        {
            if (gu_likely(wso_))
            {
                write_set_out().~WriteSetOut();
                wso_ = false;
            }
        }

        void set_deferred_abort(bool deferred_abort)
        { deferred_abort_ = deferred_abort; }
        bool deferred_abort() const { return deferred_abort_; }
    private:

        inline int pa_range_default() const
        {
            return (version() >= WriteSetNG::VER5 ? WriteSetNG::MAX_PA_RANGE :0);
        }

        inline void set_ws_flags()
        {
            uint32_t const wsrep_flags(trx_flags_to_wsrep_flags(flags()));
            uint16_t const ws_flags
                (WriteSetNG::wsrep_flags_to_ws_flags(wsrep_flags));
            write_set_out().set_flags(ws_flags);
        }

        void init_write_set_out()
        {
            assert(!wso_);
            assert(wso_buf_size_ >= sizeof(WriteSetOut));

            gu::byte_t* const wso(static_cast<gu::byte_t*>(wso_buf()));
            gu::byte_t* const store(wso + sizeof(WriteSetOut));

            assert(params_.version_ >= 0 &&
                   params_.version_ <= WriteSetNG::MAX_VERSION);

            new (wso) WriteSetOut (params_.working_dir_,
                                   trx_id(), params_.key_format_,
                                   store,
                                   wso_buf_size_ - sizeof(WriteSetOut),
                                   0,
                                   params_.record_set_ver_,
                                   WriteSetNG::Version(params_.version_),
                                   DataSet::MAX_VERSION,
                                   DataSet::MAX_VERSION,
                                   params_.max_write_set_size_);

            wso_ = true;
        }

        const WriteSetOut& write_set_out() const
        {
            return const_cast<TrxHandleMaster*>(this)->write_set_out();
        }

        TrxHandleMaster(gu::MemPool<true>&  mp,
                        const Params&       params,
                        const wsrep_uuid_t& source_id,
                        wsrep_conn_id_t     conn_id,
                        wsrep_trx_id_t      trx_id,
                        size_t              reserved_size)
            :
            TrxHandle(&trans_map_, source_id, conn_id, trx_id, params.version_),
            mutex_             (),
            mem_pool_          (mp),
            params_            (params),
            ts_                (),
            wso_buf_size_      (reserved_size - sizeof(*this)),
            gcs_handle_        (-1),
            wso_               (false),
            last_ts_seqno_     (WSREP_SEQNO_UNDEFINED),
            deferred_abort_    (false)
        {
            assert(reserved_size > sizeof(*this) + 1024);
        }

        void* wso_buf()
        {
            return static_cast<void*>(this + 1);
        }

        ~TrxHandleMaster()
        {
            release_write_set_out();
        }

        gu::Mutex              mutex_;
        gu::MemPool<true>&     mem_pool_;
        static Fsm::TransMap   trans_map_;

        Params const           params_;
        TrxHandleSlavePtr      ts_; // current fragment handle
        size_t const           wso_buf_size_;
        int                    gcs_handle_;
        bool                   wso_;
        wsrep_seqno_t          last_ts_seqno_;
        bool                   deferred_abort_;

        friend class TrxHandle;
        friend class TrxHandleSlave;
        friend class TrxHandleMasterDeleter;
        friend class TransMapBuilder<TrxHandleMaster>;

        // overrides
        TrxHandleMaster(const TrxHandleMaster&);
        TrxHandleMaster& operator=(const TrxHandleMaster&);
    };

    typedef gu::shared_ptr<TrxHandleMaster>::type TrxHandleMasterPtr;

    class TrxHandleMasterDeleter
    {
    public:
        void operator()(TrxHandleMaster* ptr)
        {
            gu::MemPool<true>& mp(ptr->mem_pool_);
            ptr->~TrxHandleMaster();
            mp.recycle(ptr);
        }
    };

    class TrxHandleLock
    {
    public:
        TrxHandleLock(TrxHandleMaster& trx)
            : trx_(trx)
            , locked_(false)
        {
            trx_.lock();
            locked_ = true;
        }
        ~TrxHandleLock()
        {
            if (locked_)
            {
                trx_.unlock();
            }
        }

        void lock()
        {
            trx_.lock();
            locked_ = true;
        }

        void unlock()
        {
            assert(locked_ = true);
            locked_ = false;
            trx_.unlock();
        }
    private:
        TrxHandleLock(const TrxHandleLock&);
        TrxHandleLock& operator=(const TrxHandleLock&);
        TrxHandleMaster& trx_;
        bool locked_;
    }; /* class TrxHnadleLock */

} /* namespace galera*/

#endif // GALERA_TRX_HANDLE_HPP
