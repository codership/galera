//
// Copyright (C) 2010-2016 Codership Oy <info@codership.com>
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
#include "gu_shared_ptr.hpp"
#include "gu_limits.h" // page size stuff

#include <set>

namespace galera
{
    static std::string const working_dir = "/tmp";

    class TrxHandle
    {
    public:
        typedef gu::shared_ptr<TrxHandle>::type SharedPtr;

        /* signed int here is to detect SIZE < sizeof(TrxHandle) */
        static size_t LOCAL_STORAGE_SIZE()
        {
            static size_t const ret(gu_page_size_multiple(1 << 13 /* 8Kb */));
            return ret;
        }

        struct Params
        {
            std::string     working_dir_;
            int             version_;
            KeySet::Version key_format_;
            int             max_write_set_size_;
            Params (const std::string& wdir, int ver, KeySet::Version kformat,
                    int max_write_set_size = WriteSetNG::MAX_SIZE) :
                working_dir_(wdir), version_(ver), key_format_(kformat),
                max_write_set_size_(max_write_set_size) {}
        };

        static const Params Defaults;

        enum Flags
        {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_ISOLATION   = 1 << 2,
            F_PA_UNSAFE   = 1 << 3,
            F_COMMUTATIVE = 1 << 4,
            F_NATIVE      = 1 << 5,
            /*
             * reserved for extension
             */
            F_PREORDERED  = 1 << 15 // flag specific to TrxHandle
        };

        static const uint32_t TRXHANDLE_FLAGS_MASK = (1 << 15) | ((1 << 7) - 1);

        static bool const FLAGS_MATCH_API_FLAGS =
                                 (WSREP_FLAG_TRX_END     == F_COMMIT       &&
                                  WSREP_FLAG_ROLLBACK    == F_ROLLBACK     &&
                                  WSREP_FLAG_ISOLATION   == F_ISOLATION    &&
                                  WSREP_FLAG_PA_UNSAFE   == F_PA_UNSAFE    &&
                                  WSREP_FLAG_COMMUTATIVE == F_COMMUTATIVE  &&
                                  WSREP_FLAG_NATIVE      == F_NATIVE);

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

        typedef enum
        {
            S_EXECUTING,
            S_MUST_ABORT,
            S_ABORTING,
            S_REPLICATING,
            S_CERTIFYING,
            S_MUST_CERT_AND_REPLAY,
            S_MUST_REPLAY_AM, // grab apply_monitor, commit_monitor, replay
            S_MUST_REPLAY_CM, // commit_monitor, replay
            S_MUST_REPLAY,    // replay
            S_REPLAYING,
            S_APPLYING,       // grabbing apply monitor, applying
            S_COMMITTING,     // grabbing commit monitor, committing changes
            S_COMMITTED,
            S_ROLLED_BACK
        } State;

        static void print_state(std::ostream&, State);

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
        };

        typedef FSM<State, Transition> Fsm;
        static Fsm::TransMap trans_map_;

        /* slave trx factory */
        typedef gu::MemPool<true> SlavePool;
        static TrxHandle* New(SlavePool& pool)
        {
            assert(pool.buf_size() == sizeof(TrxHandle));

            void* const buf(pool.acquire());

            return new(buf) TrxHandle(pool);
        }

        /* local trx factory */
        typedef gu::MemPool<true> LocalPool;
        static TrxHandle* New(LocalPool&          pool,
                              const Params&       params,
                              const wsrep_uuid_t& source_id,
                              wsrep_conn_id_t     conn_id,
                              wsrep_trx_id_t      trx_id)
        {
            size_t const buf_size(pool.buf_size());

            assert(buf_size >= (sizeof(TrxHandle) + sizeof(WriteSetOut)));

            void* const buf(pool.acquire());

            return new(buf)
                TrxHandle(pool, params, source_id, conn_id, trx_id,
                          static_cast<gu::byte_t*>(buf) + sizeof(TrxHandle),
                          buf_size - sizeof(TrxHandle));
        }

        void lock()   const { mutex_.lock();   }

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

        SharedPtr& get_shared_ptr() { return shared_ptr_; }
        void       set_shared_ptr(const SharedPtr& p) { shared_ptr_ = p; }

        int  version()     const { return version_; }

        const wsrep_uuid_t& source_id() const { return source_id_; }
        wsrep_trx_id_t      trx_id()    const { return trx_id_;    }
        wsrep_conn_id_t     conn_id()   const { return conn_id_;   }

        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }

        bool is_local()     const { return local_; }
        bool is_certified() const { return certified_; }

        void mark_certified()
        {
            int dw(0);

            if (gu_likely(depends_seqno_ >= 0))
            {
                dw = global_seqno_ - depends_seqno_;
            }

            write_set_in_.set_seqno(global_seqno_, dw);

            certified_ = true;
        }

        bool is_committed() const { return committed_; }
        void mark_committed() { committed_ = true; }

        void set_received (const void*   action,
                           wsrep_seqno_t seqno_l,
                           wsrep_seqno_t seqno_g)
        {
#ifndef NDEBUG
            if (last_seen_seqno_ >= seqno_g)
            {
                log_fatal << "S: seqno_g: " << seqno_g << ", last_seen: "
                          << last_seen_seqno_ << ", checksum: "
                          << reinterpret_cast<void*>(write_set_in_.get_checksum());
            }
            assert(last_seen_seqno_ < seqno_g);
#endif
            action_       = action;
            local_seqno_  = seqno_l;
            global_seqno_ = seqno_g;

            if (write_set_flags_ & F_PREORDERED)
            {
                assert(WSREP_SEQNO_UNDEFINED == last_seen_seqno_);
                last_seen_seqno_ = global_seqno_ - 1;
            }
        }

        void finalize(wsrep_seqno_t const last_seen_seqno)
        {
            assert (last_seen_seqno >= 0);
            assert (last_seen_seqno >= last_seen_seqno_);

            write_set_out().finalize(last_seen_seqno, 0);
            last_seen_seqno_ = last_seen_seqno;
        }

        void set_depends_seqno(wsrep_seqno_t seqno_lt)
        {
            /* make sure depends_seqno_ never goes down */
            assert(seqno_lt >= depends_seqno_ ||
                   seqno_lt == WSREP_SEQNO_UNDEFINED ||
                   preordered());
            depends_seqno_ = seqno_lt;
        }

        State state() const { return state_(); }
        void set_state(State state) { state_.shift_to(state); }

        long gcs_handle() const { return gcs_handle_; }
        void set_gcs_handle(long gcs_handle) { gcs_handle_ = gcs_handle; }

        const void* action() const { return action_; }

        wsrep_seqno_t local_seqno()     const { return local_seqno_; }

        wsrep_seqno_t global_seqno()    const { return global_seqno_; }

         // for monitor cancellation
        void set_global_seqno(wsrep_seqno_t s) { global_seqno_ = s; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t depends_seqno()   const { return depends_seqno_; }

        uint32_t      flags()           const { return write_set_flags_; }

        void set_flags(uint32_t const f)
        {
            write_set_flags_ = f;
            if (wso_)
            {
                uint32_t const wsrep_flags(trx_flags_to_wsrep_flags(flags()));
                uint16_t const ws_flags(WriteSetNG::wsrep_flags_to_ws_flags(wsrep_flags));
                write_set_out().set_flags(ws_flags);
            }
        }

        void append_key(const KeyData& key)
        {
            // Current limitations with certification on trx version 3
            // impose the the following restrictions on keys

            // The shared key behavior for TOI operations is completely
            // untested, so don't allow it (and it probably does not even
            // make any sense)
            assert(is_toi() == false  || key.shared() == false);
            // Shared key escalation to level 1 or 2 is not allowed
            assert(key.parts_num == 3 || key.shared() == false);
            // For key level less than 3 write set must be TOI because
            // conflicts between different key levels are not detected
            // correctly even if both keys are exclusive
            assert(key.parts_num == 3 || is_toi() == true);

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
                gu_trace(write_set_out().append_unordered(data,data_len,store));
                break;
            case WSREP_DATA_ANNOTATION:
                gu_trace(write_set_out().append_annotation(data,data_len,store));
                break;
            }
        }

        bool empty() const
        {
            return write_set_out().is_empty();
        }

        WriteSetOut& write_set_out()
        {
            /* WriteSetOut is a temporary object needed only at the writeset
             * collection stage. Since it may allocate considerable resources
             * we dont't want it to linger as long as TrxHandle is needed and
             * want to destroy it ASAP. So it is located immediately after
             * TrxHandle in the buffer allocated by TrxHandleWithStore.
             * I'll be damned if this+1 is not sufficiently well aligned. */
            assert(wso_);
            return *reinterpret_cast<WriteSetOut*>(this + 1);
        }
        const WriteSetOut& write_set_out() const
        {
            return const_cast<TrxHandle*>(this)->write_set_out();
        }

        const WriteSetIn&  write_set_in () const { return write_set_in_;  }

        void apply(void*                   recv_ctx,
                   wsrep_apply_cb_t        apply_cb,
                   const wsrep_trx_meta_t& meta) const /* throws */;

        void unordered(void*                recv_ctx,
                       wsrep_unordered_cb_t apply_cb) const;

        void verify_checksum() const /* throws */
        {
            write_set_in_.verify_checksum();
        }

        uint64_t get_checksum() const
        {
            return write_set_in_.get_checksum();
        }

        size_t size() const
        {
            return write_set_in_.size();
        }

        void update_stats(gu::Atomic<long long>& kc,
                          gu::Atomic<long long>& kb,
                          gu::Atomic<long long>& db,
                          gu::Atomic<long long>& ub)
        {
            kc += write_set_in_.keyset().count();
            kb += write_set_in_.keyset().size();
            db += write_set_in_.dataset().size();
            ub += write_set_in_.unrdset().size();
        }

        bool   exit_loop() const { return exit_loop_; }
        void   set_exit_loop(bool x) { exit_loop_ |= x; }
        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

        void release_write_set_out()
        {
            if (gu_likely(wso_))
            {
                write_set_out().~WriteSetOut();
                wso_ = false;
            }
        }

        void mark_dummy()
        {
            set_depends_seqno(WSREP_SEQNO_UNDEFINED);
            set_flags(flags() | F_ROLLBACK);
            assert(state() == S_CERTIFYING  ||
                   state() == S_REPLICATING ||
                   state() == S_MUST_ABORT);
            if (state() == S_REPLICATING) set_state(S_MUST_ABORT);
            set_state(S_ABORTING);
            // must be set to S_ROLLED_BACK after commit_cb()
        }
        bool is_dummy()   const { return (flags() &  F_ROLLBACK); }
        bool skip_event() const { return (flags() == F_ROLLBACK); }

        void print(std::ostream& os) const;

    private:

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

            return ret;
        }

        /* slave trx ctor */
        explicit
        TrxHandle(gu::MemPool<true>& mp)
            :
            source_id_         (WSREP_UUID_UNDEFINED),
            conn_id_           (-1),
            trx_id_            (-1),
            mutex_             (),
            shared_ptr_        (),
            state_             (&trans_map_, S_REPLICATING),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            timestamp_         (),
            write_set_in_      (),
            mem_pool_          (mp),
            action_            (0),
            gcs_handle_        (-1),
            version_           (Defaults.version_),
            write_set_flags_   (0),
            local_             (false),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false),
            wso_               (false)
        {}

        /* local trx ctor */
        TrxHandle(gu::MemPool<true>&  mp,
                  const Params&       params,
                  const wsrep_uuid_t& source_id,
                  wsrep_conn_id_t     conn_id,
                  wsrep_trx_id_t      trx_id,
                  gu::byte_t*         reserved,
                  size_t              reserved_size)
            :
            source_id_         (source_id),
            conn_id_           (conn_id),
            trx_id_            (trx_id),
            mutex_             (),
            shared_ptr_        (),
            state_             (&trans_map_, S_EXECUTING),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            timestamp_         (gu_time_calendar()),
            write_set_in_      (),
            mem_pool_          (mp),
            action_            (0),
            gcs_handle_        (-1),
            version_           (params.version_),
            write_set_flags_   (0),
            local_             (true),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false),
            wso_               (true)
        {
            init_write_set_out(params, reserved, reserved_size);
        }

        ~TrxHandle() { if (wso_) release_write_set_out(); }

        void
        init_write_set_out(const Params& params,
                           gu::byte_t*   store,
                           size_t        store_size)
        {
            if (wso_)
            {
                assert(store);
                assert(store_size > sizeof(WriteSetOut));

                WriteSetOut* wso = &write_set_out();
                assert(static_cast<void*>(wso) == static_cast<void*>(store));

                new (wso) WriteSetOut (params.working_dir_,
                                       trx_id_, params.key_format_,
                                       store      + sizeof(WriteSetOut),
                                       store_size - sizeof(WriteSetOut),
                                       0,
                                       WriteSetNG::MAX_VERSION,
                                       DataSet::MAX_VERSION,
                                       DataSet::MAX_VERSION,
                                       params.max_write_set_size_);
            }
        }

        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        mutable gu::Mutex      mutex_;
        SharedPtr              shared_ptr_;
        FSM<State, Transition> state_;
        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          depends_seqno_;
        int64_t                timestamp_;
        WriteSetIn             write_set_in_;

        gu::MemPool<true>&     mem_pool_;
        const void*            action_;
        long                   gcs_handle_;
        int                    version_;
        uint32_t               write_set_flags_;
        bool                   local_;
        bool                   certified_;
        bool                   committed_;
        bool                   exit_loop_;
        bool                   wso_;

        friend class TrxHandleWithStore;
        friend class TrxHandleDeleter;

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

    typedef TrxHandle::SharedPtr TrxHandlePtr;

    class TrxHandleDeleter
    {
    public:
        void operator()(TrxHandle* ptr)
        {
            gu::MemPool<true>& mp(ptr->mem_pool_);
            ptr->~TrxHandle();
            mp.recycle(ptr);
        }
    };

    class TrxHandleLock
    {
    public:
        TrxHandleLock(TrxHandle& trx) : trx_(trx) { trx_.lock(); }
        ~TrxHandleLock() { trx_.unlock(); }
    private:
        TrxHandle& trx_;

    }; /* class TrxHnadleLock */

} /* namespace galera*/

#endif // GALERA_TRX_HANDLE_HPP
