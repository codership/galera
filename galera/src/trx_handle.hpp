//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
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

#include <set>

namespace galera
{
    static std::string const working_dir = "/tmp";

    static int const WS_NG_VERSION = WriteSetNG::VER3;
    /* new WS version to be used */

    class TrxHandle
    {
    public:

        /* signed int here is to detect SIZE < sizeof(TrxHandle) */
        static int const SIZE = GU_PAGE_SIZE * 2; // 8K

#define TRX_HANDLE_STORE_SIZE \
        (TrxHandle::SIZE - static_cast<int>(sizeof(TrxHandle)))

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

        // Placeholder for message authentication code
        class Mac
        {
        public:
            Mac() { }
            ~Mac() { }

            size_t serialize(gu::byte_t* buf, size_t buflen, size_t offset)
                const;
            size_t unserialize(const gu::byte_t* buf, size_t buflen,
                               size_t offset);
            size_t serial_size() const;
        };

        /* slave trx ctor */
        TrxHandle()
            :
            source_id_         (WSREP_UUID_UNDEFINED),
            conn_id_           (-1),
            trx_id_            (-1),
            mutex_             (),
            write_set_collection_(Defaults.working_dir_),
            state_             (&trans_map_, S_EXECUTING),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            timestamp_         (),
            write_set_         (Defaults.version_),
            write_set_in_      (),
            annotation_        (),
            cert_keys_         (),
            write_set_buffer_  (0, 0),
            action_            (0),
            gcs_handle_        (-1),
            version_           (Defaults.version_),
            refcnt_            (1),
            write_set_flags_   (0),
            local_             (false),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false),
            wso_               (false),
            mac_               ()
        {}

        /* local trx ctor */
        explicit
        TrxHandle(const Params&       params,
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
            write_set_collection_(params.working_dir_),
            state_             (&trans_map_, S_EXECUTING),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            timestamp_         (gu_time_calendar()),
            write_set_         (params.version_),
            write_set_in_      (),
            annotation_        (),
            cert_keys_         (),
            write_set_buffer_  (0, 0),
            action_            (0),
            gcs_handle_        (-1),
            version_           (params.version_),
            refcnt_            (1),
            write_set_flags_   (0),
            local_             (true),
            certified_         (false),
            committed_         (false),
            exit_loop_         (false),
            wso_               (new_version()),
            mac_               ()
        {
            /* Can't do this now as TrxHandleWithStore initializes store AFTER
             * this ctor, ruining everything.
             * Move it here after converting to memory pool. */
            // init_write_set_out(params, reserved, reserved_size);
        }

        void lock()   const { mutex_.lock();   }
        void unlock() const { mutex_.unlock(); }

        int  version()     const { return version_; }
        bool new_version() const { return version() >= WS_NG_VERSION; }

        const wsrep_uuid_t& source_id() const { return source_id_; }
        wsrep_trx_id_t      trx_id()    const { return trx_id_;    }
        wsrep_conn_id_t     conn_id()   const { return conn_id_;   }

        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }

        bool is_local()     const { return local_; }
        bool is_certified() const { return certified_; }

        void mark_certified()
        {
            if (new_version())
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

        bool is_committed() const { return committed_; }
        void mark_committed() { committed_ = true; }

        void set_received (const void*   action,
                           wsrep_seqno_t seqno_l,
                           wsrep_seqno_t seqno_g)
        {
            action_       = action;
            local_seqno_  = seqno_l;
            global_seqno_ = seqno_g;
            if (write_set_flags_ & F_PREORDERED)
            {
                last_seen_seqno_ = global_seqno_ - 1;
            }
        }

        void set_last_seen_seqno(wsrep_seqno_t last_seen_seqno)
        {
            assert (last_seen_seqno >= 0);
            if (new_version())
                write_set_out().set_last_seen(last_seen_seqno);
            last_seen_seqno_ = last_seen_seqno;
        }

        void set_depends_seqno(wsrep_seqno_t seqno_lt)
        {
            depends_seqno_ = seqno_lt;
        }

        State state() const { return state_(); }
        void set_state(State state) { state_.shift_to(state); }

        long gcs_handle() const { return gcs_handle_; }
        void set_gcs_handle(long gcs_handle) { gcs_handle_ = gcs_handle; }

        const void* action() const { return action_; }

        wsrep_seqno_t local_seqno()     const { return local_seqno_; }

        wsrep_seqno_t global_seqno()    const { return global_seqno_; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t depends_seqno()   const { return depends_seqno_; }

        uint32_t      flags()           const { return write_set_flags_; }

        void set_flags(uint32_t flags)
        {
            write_set_flags_ = flags;

            if (new_version())
            {
                uint16_t ws_flags(flags & 0x07);
                if (flags & F_ISOLATION) ws_flags |= WriteSetNG::F_TOI;
                if (flags & F_PA_UNSAFE) ws_flags |= WriteSetNG::F_PA_UNSAFE;
                write_set_out().set_flags(ws_flags);
            }
        }

        void append_key(const KeyData& key)
        {
            /*! protection against protocol change during trx lifetime */
            if (key.proto_ver != version_)
            {
                gu_throw_error(EINVAL) << "key version '" << key.proto_ver
                                       << "' does not match to trx version' "
                                       << version_ << "'";
            }

            if (new_version())
            {
                write_set_out().append_key(key);
            }
            else
            {
                write_set_.append_key(key);
            }
        }

        void append_data(const void* data, const size_t data_len,
                         wsrep_data_type_t type, bool store)
        {
            if (new_version())
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
                }
            }
            else
            {
                switch (type)
                {
                case WSREP_DATA_ORDERED:
                    write_set_.append_data(data, data_len);
                    break;
                case WSREP_DATA_UNORDERED:
                    // just ignore unordered for compatibility with
                    // previous versions
                    break;
                case WSREP_DATA_ANNOTATION:
                    append_annotation(reinterpret_cast<const gu::byte_t*>(data),
                                      data_len);
                    break;
                }
            }
        }

        static const size_t max_annotation_size_ = (1 << 16);

        void append_annotation(const gu::byte_t* buf, size_t buf_len)
        {
            buf_len = std::min(buf_len,
                               max_annotation_size_ - annotation_.size());
            annotation_.insert(annotation_.end(), buf, buf + buf_len);
        }

        const gu::Buffer& annotation() const { return annotation_; }

        const WriteSet& write_set() const { return write_set_; }

        size_t prepare_write_set_collection()
        {
            if (new_version()) assert(0);

            size_t offset;
            if (write_set_collection_.empty() == true)
            {
                offset = serial_size();
                write_set_collection_.resize(offset);
            }
            else
            {
                offset = write_set_collection_.size();
            }
            (void)serialize(&write_set_collection_[0], offset, 0);
            return offset;
        }

        void append_write_set(const void* data, size_t data_len)
        {
            if (new_version()) assert(0);

            const size_t offset(prepare_write_set_collection());
            write_set_collection_.resize(offset + data_len);
            std::copy(reinterpret_cast<const gu::byte_t*>(data),
                      reinterpret_cast<const gu::byte_t*>(data) + data_len,
                      &write_set_collection_[0] + offset);
        }

        void append_write_set(const gu::Buffer& ws)
        {
            if (new_version())
            {
                /* trx->unserialize() must have done all the job */
            }
            else
            {
                const size_t offset(prepare_write_set_collection());
                write_set_collection_.resize(offset + ws.size());
                std::copy(ws.begin(), ws.end(),
                          &write_set_collection_[0] + offset);
            }
        }

        MappedBuffer& write_set_collection()
        {
            return write_set_collection_;
        }

        void set_write_set_buffer(const gu::byte_t* buf, size_t buf_len)
        {
            write_set_buffer_.first  = buf;
            write_set_buffer_.second = buf_len;
        }

        std::pair<const gu::byte_t*, size_t>
        write_set_buffer() const
        {
            // If external write set buffer location not specified,
            // return location from write_set_collection_. This is still
            // needed for unit tests and IST which don't use GCache
            // storage.
            if (write_set_buffer_.first == 0)
            {
                size_t off(serial_size());
                if (write_set_collection_.size() < off)
                {
                    gu_throw_fatal << "Write set buffer not populated";
                }
                return std::make_pair(&write_set_collection_[0] + off,
                                      write_set_collection_.size() - off);
            }
            return write_set_buffer_;
        }

        bool empty() const
        {
            if (new_version())
            {
                return write_set_out().is_empty();
            }
            else
            {
                return (write_set_.empty() == true &&
                        write_set_collection_.size() <= serial_size());
            }
        }

        void flush(size_t mem_limit)
        {
            if (new_version()) { assert(0); return; }

            if (write_set_.get_key_buf().size() + write_set_.get_data().size()
                > mem_limit || mem_limit == 0)
            {
                gu::Buffer buf(write_set_.serial_size());
                (void)write_set_.serialize(&buf[0], buf.size(), 0);
                append_write_set(buf);
                write_set_.clear();
            }
        }

        void clear()
        {
            if (new_version()) { return; }

            write_set_.clear();
            write_set_collection_.clear();
        }

        void   ref()   { ++refcnt_; }
        void   unref() { if (refcnt_.sub_and_fetch(1) == 0) delete this; }
        size_t refcnt() const { return refcnt_(); }

        WriteSetOut& write_set_out()
        {
            /* WriteSetOut is a temporary object needed only at the writeset
             * collection stage. Since it may allocate considerable resources
             * we dont't want it to linger as long as TrxHandle is needed and
             * want to destroy it ASAP. So it is located immediately after
             * TrxHandle in the buffer allocated by TrxHandleWithStore.
             * I'll be damned if this+1 is not sufficiently well aligned. */
            assert(new_version());
            assert(wso_);
            return *reinterpret_cast<WriteSetOut*>(this + 1);
        }
        const WriteSetOut& write_set_out() const { return write_set_out(); }

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

        void update_stats(gu::Atomic<long long>& kc,
                          gu::Atomic<long long>& kb,
                          gu::Atomic<long long>& db,
                          gu::Atomic<long long>& ub)
        {
            assert(new_version());
            kc += write_set_in_.keyset().count();
            kb += write_set_in_.keyset().size();
            db += write_set_in_.dataset().size();
            ub += write_set_in_.unrdset().size();
        }

        bool   exit_loop() const { return exit_loop_; }
        void   set_exit_loop(bool x) { exit_loop_ |= x; }

        typedef gu::UnorderedMap<KeyEntryOS*,
                                 std::pair<bool, bool>,
                                 KeyEntryPtrHash,
                                 KeyEntryPtrEqualAll> CertKeySet;

        CertKeySet& cert_keys() { return cert_keys_; }

        size_t serial_size() const;
        size_t serialize  (gu::byte_t* buf, size_t buflen, size_t offset) const;
        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

        void release_write_set_out()
        {
            if (gu_likely(new_version()))
            {
                assert(wso_);
                write_set_out().~WriteSetOut();
                wso_ = false;
            }
        }

    private:

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
        MappedBuffer           write_set_collection_;
        FSM<State, Transition> state_;
        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          depends_seqno_;
        int64_t                timestamp_;
        WriteSet               write_set_;
        WriteSetIn             write_set_in_;
        gu::Buffer             annotation_;
        CertKeySet             cert_keys_;

        // Write set buffer location if stored outside TrxHandle.
        std::pair<const gu::byte_t*, size_t> write_set_buffer_;

        const void*            action_;
        long                   gcs_handle_;
        int                    version_;
        gu::Atomic<int>        refcnt_;
        uint32_t               write_set_flags_;
        bool                   local_;
        bool                   certified_;
        bool                   committed_;
        bool                   exit_loop_;
        bool                   wso_;
        Mac                    mac_;

        friend class Wsdb;
        friend class Certification;
        friend std::ostream& operator<<(std::ostream& os, const TrxHandle& trx);
        friend class TrxHandleWithStore;

    }; /* class TrxHandle */

    std::ostream& operator<<(std::ostream& os, TrxHandle::State s);

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

    /* Normally this should have been simply DERIVED from TrxHandle and have
     * a proper initialization order. However to do it properly would require
     * more refactoring than is desirable ATM. Postponing this for 4.x */
    class TrxHandleWithStore
    {
    public:

        TrxHandleWithStore(const TrxHandle::Params& params,
                           const wsrep_uuid_t&      source_id,
                           wsrep_conn_id_t          conn_id,
                           wsrep_trx_id_t           trx_id) :
            trx_  (params, source_id, conn_id, trx_id, store_, sizeof(store_)),
            store_()
        {
            trx_.init_write_set_out(params, store_, sizeof(store_));
        }

        TrxHandle* handle() { return &trx_; }

    private:

        TrxHandle  trx_;
        gu::byte_t store_[TRX_HANDLE_STORE_SIZE];
    }; /* class TrxHandleWithStore */

    struct TrxHandleCompileAssert
    {
        GU_COMPILE_ASSERT(
            (sizeof(TrxHandleWithStore) == TrxHandle::SIZE), size_match);
    };

} /* namespace galera*/

#endif // GALERA_TRX_HANDLE_HPP
