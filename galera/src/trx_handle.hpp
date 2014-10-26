//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
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

#include <set>

namespace galera
{
    static std::string const working_dir = "/tmp";

    static int const WS_NG_VERSION = WriteSetNG::VER3;
    /* new WS version to be used */

    class TrxHandle
    {
    public:

        enum Flags
      {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_OOC         = 1 << 2,
            F_MAC_HEADER  = 1 << 3,
            F_MAC_PAYLOAD = 1 << 4,
            F_ANNOTATION  = 1 << 5,
            F_ISOLATION   = 1 << 6,
            F_PA_UNSAFE   = 1 << 7,
            F_PREORDERED  = 1 << 8
        };

        static inline uint32_t wsrep_flags_to_trx_flags (uint32_t flags)
        {
            GU_COMPILE_ASSERT(
                WSREP_FLAG_COMMIT   == int(F_COMMIT)   && F_COMMIT   == 1 &&
                WSREP_FLAG_ROLLBACK == int(F_ROLLBACK) && F_ROLLBACK == 2,
                flags_dont_match1);

            uint32_t ret(flags & 0x03); // setting F_COMMIT|F_ROLLBACK in one go

            if (flags & WSREP_FLAG_ISOLATION)   ret |= F_ISOLATION;
            if (flags & WSREP_FLAG_PA_UNSAFE)   ret |= F_PA_UNSAFE;

            return ret;
        }

        static inline uint32_t trx_flags_to_wsrep_flags (uint32_t flags)
        {
            GU_COMPILE_ASSERT(
                WSREP_FLAG_COMMIT   == int(F_COMMIT)   && F_COMMIT   == 1 &&
                WSREP_FLAG_ROLLBACK == int(F_ROLLBACK) && F_ROLLBACK == 2,
                flags_dont_match2);

            uint32_t ret(flags & 0x03); // setting F_COMMIT|F_ROLLBACK in one go

            if (flags & F_ISOLATION)   ret |= WSREP_FLAG_ISOLATION;
            if (flags & F_PA_UNSAFE)   ret |= WSREP_FLAG_PA_UNSAFE;

            return ret;
        }

        static inline uint32_t wsng_flags_to_trx_flags (uint32_t flags)
        {
            GU_COMPILE_ASSERT(
                WriteSetNG::F_COMMIT   == int(F_COMMIT)   && F_COMMIT   == 1 &&
                WriteSetNG::F_ROLLBACK == int(F_ROLLBACK) && F_ROLLBACK == 2,
                flags_dont_match3);

            uint32_t ret(flags & 0x03); // setting F_COMMIT|F_ROLLBACK in one go

            if (flags & WriteSetNG::F_TOI)       ret |= F_ISOLATION;
            if (flags & WriteSetNG::F_PA_UNSAFE) ret |= F_PA_UNSAFE;

            return ret;
        }

        bool has_mac() const
        {
            return ((write_set_flags_ & (F_MAC_HEADER | F_MAC_PAYLOAD)) != 0);
        }

        bool has_annotation() const /* shall return 0 for new writeset ver */
        {
            return ((write_set_flags_ & F_ANNOTATION) != 0);
        }

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
            S_APPLYING,   // grabbing apply monitor, applying
            S_COMMITTING, // grabbing commit monitor, committing changes
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
        static Fsm::TransMap trans_map_master;
        static Fsm::TransMap trans_map_slave;

        virtual void lock()   const = 0;
#ifndef NDEBUG
        virtual bool locked() const = 0;
#endif
        virtual void unlock() const = 0;

        int  version()     const { return version_; }

        const wsrep_uuid_t& source_id() const { return source_id_; }
        wsrep_trx_id_t      trx_id()    const { return trx_id_;    }
        bool                is_local()  const { return local_; }

        wsrep_conn_id_t conn_id() const { return conn_id_;   }
        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }

        State state() const { return state_(); }
        void  set_state(State state)
        {
#ifndef NDEBUG
//            print_set_state(state);
            assert(locked());
#endif
            state_.shift_to(state);
        }

        void print_set_state(State state) const;

        uint32_t flags() const { return write_set_flags_; }
        void set_flags(uint32_t flags) { write_set_flags_ = flags; }

        uint64_t timestamp() const { return timestamp_; }

        virtual void   ref()   = 0;
        virtual void   unref() = 0;

        bool local() const { return local_; }

        virtual int refcnt() const  = 0;

        void print(std::ostream& os) const;

        virtual ~TrxHandle() {}

    protected:

        /* slave trx ctor */
        explicit
        TrxHandle()
            :
            source_id_         (WSREP_UUID_UNDEFINED),
            conn_id_           (-1),
            trx_id_            (-1),
            state_             (&trans_map_slave, S_REPLICATING),
            timestamp_         (),
            version_           (-1),
            write_set_flags_   (0),
            local_             (false)
        {}

        /* local trx ctor */
        TrxHandle(const wsrep_uuid_t& source_id,
                  wsrep_conn_id_t     conn_id,
                  wsrep_trx_id_t      trx_id,
                  int                 version)
            :
            source_id_         (source_id),
            conn_id_           (conn_id),
            trx_id_            (trx_id),
            state_             (&trans_map_master, S_EXECUTING),
            timestamp_         (gu_time_calendar()),
            version_           (version),
            write_set_flags_   (0),
            local_             (true)
        {}

        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        FSM<State, Transition> state_;
        int64_t                timestamp_;
        int                    version_;
        uint32_t               write_set_flags_;
        bool                   local_;

    private:

        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        friend class Wsdb;
        friend class Certification;

    }; /* class TrxHandle */

    std::ostream& operator<<(std::ostream& os, TrxHandle::State s);
    std::ostream& operator<<(std::ostream& os, const TrxHandle& trx);

    class TrxHandleSlave : public TrxHandle
    {
    public:

        typedef gu::MemPool<true> Pool;
        static TrxHandleSlave* New(Pool& pool)
        {
            assert(pool.buf_size() == sizeof(TrxHandleSlave));

            void* const buf(pool.acquire());

        return new(buf) TrxHandleSlave(pool, buf);
        }

        void lock()   const { mutex_.lock(); }
#ifndef NDEBUG
        bool locked() const { return mutex_.locked(); }
#endif /* NDEBUG */
        void unlock() const
        {
            assert(locked());
            mutex_.unlock();
        }

        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

        void verify_checksum() const /* throws */
        {
            write_set_in_.verify_checksum();
        }

        void update_stats(gu::Atomic<long long>& kc,
                          gu::Atomic<long long>& kb,
                          gu::Atomic<long long>& db,
                          gu::Atomic<long long>& ub)
        {
            assert(version() >= WS_NG_VERSION);

            kc += write_set_in_.keyset().count();
            kb += write_set_in_.keyset().size();
            db += write_set_in_.dataset().size();
            ub += write_set_in_.unrdset().size();
        }

        void set_received (const void*   action,
                           wsrep_seqno_t seqno_l,
                           wsrep_seqno_t seqno_g)
        {
            action_       = action;
            local_seqno_  = seqno_l;
            global_seqno_ = seqno_g;

            if (flags() & F_PREORDERED)
            {
                last_seen_seqno_ = global_seqno_ - 1;
            }
        }

        bool is_certified() const { return certified_; }

        void mark_certified()
        {
            if (write_set_in_.size() > 0)
            {
                int dw(0);

                if (gu_likely(depends_seqno_ >= 0))
                {
                    dw = global_seqno_ - depends_seqno_;
                }

                write_set_in_.set_seqno(global_seqno_, dw);
            }

            certified_ = true;
        }

        void set_depends_seqno(wsrep_seqno_t seqno_lt)
        {
            depends_seqno_ = seqno_lt;
        }

        void apply(void*                   recv_ctx,
                   wsrep_apply_cb_t        apply_cb,
                   const wsrep_trx_meta_t& meta) const /* throws */;

        bool is_committed() const { return committed_; }
        void mark_committed()     { committed_ = true; }

        void unordered(void*                recv_ctx,
                       wsrep_unordered_cb_t apply_cb) const;

        const void*   action()          const { return action_; }

        wsrep_seqno_t local_seqno()     const { return local_seqno_; }

        wsrep_seqno_t global_seqno()    const { return global_seqno_; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t depends_seqno()   const { return depends_seqno_; }

        const WriteSetIn&  write_set_in () const { return write_set_in_;  }

        bool   exit_loop() const { return exit_loop_; }
        void   set_exit_loop(bool x) { exit_loop_ |= x; }

        typedef gu::UnorderedMap<KeyEntryOS*,
                                 std::pair<bool, bool>,
                                 KeyEntryPtrHash,
                                 KeyEntryPtrEqualAll> CertKeySet;

        CertKeySet& cert_keys() { return cert_keys_; }

        void print(std::ostream& os) const;

        void ref()   { ++refcnt_; }
        void unref()
        {
            if (refcnt_.sub_and_fetch(1) == 0) // delete and return to pool
            {
                assert(!locked());

                void* const buf(buf_);
                gu::MemPool<true>& mp(mem_pool_);

#if 1 // use if()
                if (buf == static_cast<void*>(this))
                {
                    this->~TrxHandleSlave();
                }
                else
                {
                    destroy_local(buf);
                }
#else // use virtual dtor
                (static_cast<TrxHandle*>(buf))->~TrxHandle();
#endif

                mp.recycle(buf);
            }
        }

        int refcnt() const { return refcnt_(); }

    protected:

        explicit
        TrxHandleSlave(gu::MemPool<true>& mp, void* buf) :
            TrxHandle          (),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            mem_pool_          (mp),
            write_set_in_      (),
            cert_keys_         (),
            mutex_             (),
            buf_               (buf),
            action_            (0),
            refcnt_            (1),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false)
        {}

        friend class TrxHandleMaster;

    private:

        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          depends_seqno_;
        gu::MemPool<true>&     mem_pool_;
        WriteSetIn             write_set_in_;
        CertKeySet             cert_keys_;
        gu::Mutex mutable      mutex_;
        void* const            buf_;
        const void*            action_;
        gu::Atomic<int>        refcnt_;
        bool                   certified_;
        bool                   committed_;
        bool                   exit_loop_;

        TrxHandleSlave(const TrxHandleSlave&);
        void operator=(const TrxHandleSlave& other);

        ~TrxHandleSlave() { }

        void destroy_local(void* ptr);

    }; /* TrxHandleSlave */

    std::ostream& operator<<(std::ostream& os, const TrxHandleSlave& trx);

    class TrxHandleMaster : public TrxHandle
    {
    public:
        /* signed int here is to detect SIZE < sizeof(TrxHandle) */
        static int const LOCAL_STORAGE_SIZE = GU_PAGE_SIZE * 2; // 8K

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

            Params () :
                working_dir_(), version_(), key_format_(), max_write_set_size_()
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

        void lock()   const
        {
            assert(repl_.size() > 0);
            repl_.back()->lock();
        }

#ifndef NDEBUG
        bool locked() const { return repl_.back()->locked(); }
#endif /* NDEBUG */

        void unlock() const
        {
            assert(repl_.size() > 0);
            assert(locked());
            repl_.back()->unlock();
        }

        void set_state(TrxHandle::State const s)
        {
            TrxHandle::set_state(s);
            if (gu_unlikely(TrxHandle::S_MUST_ABORT == s))
            {
                if (tr_.state() != s) tr_.set_state(s);
            }
        }

        long gcs_handle() const { return gcs_handle_; }
        void set_gcs_handle(long gcs_handle) { gcs_handle_ = gcs_handle; }

        void set_flags(uint32_t flags)
        {
            TrxHandle::set_flags(flags);

            uint16_t ws_flags(flags & 0x07);
            if (flags & F_ISOLATION) ws_flags |= WriteSetNG::F_TOI;
            if (flags & F_PA_UNSAFE) ws_flags |= WriteSetNG::F_PA_UNSAFE;
            write_set_out().set_flags(ws_flags);
        }

        void append_key(const KeyData& key)
        {
            /*! protection against protocol change during trx lifetime */
            if (key.proto_ver != version())
            {
                gu_throw_error(EINVAL) << "key version '" << key.proto_ver
                                       << "' does not match to trx version' "
                                       << version() << "'";
            }

            write_set_out().append_key(key);
        }

        void append_data(const void* data, const size_t data_len,
                         wsrep_data_type_t type, bool store)
        {
            switch (type)
            {
            case WSREP_DATA_ORDERED:
                write_set_out().append_data(data, data_len, store);
                break;
            case WSREP_DATA_UNORDERED:
                write_set_out().append_unordered(data, data_len, store);
                break;
            case WSREP_DATA_ANNOTATION:
                write_set_out().append_annotation(data, data_len, store);
                break;
            };
        }

        bool empty() const
        {
            return write_set_out().is_empty();
        }

        void set_last_seen_seqno(wsrep_seqno_t last_seen_seqno)
        {
            assert (last_seen_seqno >= 0);
            write_set_out().set_last_seen(last_seen_seqno);
        }

        /* Serializes wiriteset into a single buffer (for unit test purposes) */
        void serialize(const wsrep_seqno_t&     last_seen,
                       std::vector<gu::byte_t>& ret)
        {
            write_set_out().serialize(source_id(), conn_id(), trx_id(),
                                      last_seen, ret);
        }

        void clear()
        {
            release_write_set_out();
        }

        typedef gu::Vector<TrxHandleSlave*, 1> ReplVector;

        const ReplVector& replicated() const
        {
            return repl_;
        }

        wsrep_seqno_t global_seqno() const
        {
            if (gu_likely(repl_.size() > 0))
                return repl_.back()->global_seqno();
            else
                return WSREP_SEQNO_UNDEFINED;
        }

        wsrep_seqno_t last_seen_seqno() const
        {
            if (gu_likely(repl_.size() > 0))
                return repl_.back()->last_seen_seqno();
            else
                return WSREP_SEQNO_UNDEFINED;
        }

        void add_replicated(TrxHandleSlave* const ts)
        {
            assert(locked());
            assert(ts->locked());
            ts->ref();
            repl_.push_back(ts);
            // unlock previous fragment
            if (repl_.size() > 1) repl_[repl_.size() - 2]->unlock();
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

        void ref()   { tr_.ref();   }
        void unref() { tr_.unref(); }

        int  refcnt() const { return tr_.refcnt(); }

    private:

        void init_write_set_out()
        {
            assert(!wso_);
            assert(wso_buf_size_ > sizeof(WriteSetOut));

            gu::byte_t* const wso(static_cast<gu::byte_t*>(wso_buf()));
            gu::byte_t* const store(wso + sizeof(WriteSetOut));

            new (wso) WriteSetOut (params_.working_dir_,
                                   trx_id(), params_.key_format_,
                                   store,
                                   wso_buf_size_ - sizeof(WriteSetOut),
                                   0,
                                   WriteSetNG::MAX_VERSION,
                                   DataSet::MAX_VERSION,
                                   DataSet::MAX_VERSION,
                                   params_.max_write_set_size_);

            wso_ = true;
        }

        const WriteSetOut& write_set_out() const { return write_set_out(); }

        TrxHandleMaster(gu::MemPool<true>&  mp,
                        const Params&       params,
                        const wsrep_uuid_t& source_id,
                        wsrep_conn_id_t     conn_id,
                        wsrep_trx_id_t      trx_id,
                        size_t              reserved_size)
            :
            TrxHandle(source_id, conn_id, trx_id, params.version_),
            params_            (params),
            tr_                (mp, this),
            repl_              (),
            wso_buf_size_      (reserved_size - sizeof(*this)),
            gcs_handle_        (-1),
            wso_               (false)
        {
            assert(reserved_size > sizeof(*this) + 1024);

            /* There will be at least (and normally) one replicated writeset per
             * transaction. We initialized its handle in initialization list
             * and now add it as the first element to the vector that will hold
             * pointers to all such handles belonging to this transaction. */
            assert(repl_.size() == 0);
            repl_.push_back(&tr_);
            assert(NULL != repl_[0]);
            assert(!repl_.in_heap()); //there should happen no dynamic allocation
            assert(repl_.size() == 1);
        }

        void* wso_buf()
        {
            return static_cast<void*>(this + 1);
        }

        ~TrxHandleMaster()
        {
            release_write_set_out();

            assert(refcnt() == 0);
            assert(repl_[0] == &tr_);
            assert(repl_[0]->refcnt() == 0);

            for (unsigned int i(1); i < repl_.size(); ++i)
            {
                repl_[i]->unref();
            }
        }

        Params const           params_;
        TrxHandleSlave         tr_;
        ReplVector             repl_;
        size_t const           wso_buf_size_;
        long                   gcs_handle_;
        bool                   wso_;

        friend class TrxHandleSlave;
    };

    class TrxHandleLock
    {
    public:
        TrxHandleLock(TrxHandle& trx) : trx_(trx) { trx_.lock(); }
        ~TrxHandleLock() { trx_.unlock(); }
    private:
        TrxHandle& trx_;

    }; /* class TrxHnadleLock */

    template <typename T>
    class Unref2nd
    {
    public:
        void operator()(T& t) const { t.second->unref(); }
    };

} /* namespace galera*/

#endif // GALERA_TRX_HANDLE_HPP
