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

#include <set>

namespace galera
{

//    class KeyEntry; // Forward declaration

    static std::string const working_dir = "/tmp";

    // this is an overall replicator-level protocol version which employs new
    // writeset format. See replicator_smm.hpp. Note that writeset version is 3.
//    static int const NEW_WRITE_SET_PROTO_VER = 5;

    static int const WS_NG_VERSION = WriteSetNG::VER3;
    /* new WS version to be used */

    class TrxHandle
    {
    public:
        enum
        {
            F_COMMIT      = 1 << 0,
            F_ROLLBACK    = 1 << 1,
            F_OOC         = 1 << 2,
            F_MAC_HEADER  = 1 << 3,
            F_MAC_PAYLOAD = 1 << 4,
            F_ANNOTATION  = 1 << 5,
            F_ISOLATION   = 1 << 6,
            F_PA_UNSAFE   = 1 << 7
        };

        bool has_mac() const /* shall return 0 for new writeset ver */
        {
            return ((write_set_flags_ & (F_MAC_HEADER | F_MAC_PAYLOAD)) != 0);
        }

        bool has_annotation() const /* shall return 0 for new writeset ver */
        {
            return ((write_set_flags_ & F_ANNOTATION) != 0);
        }

        bool is_toi() const
        {
            if (new_version())
            {
                return write_set_in_.is_toi();
            }
            else
            {
                return ((write_set_flags_ & F_ISOLATION) != 0);
            }
        }

        bool pa_safe() const
        {
            if (new_version())
            {
                return !write_set_in_.pa_unsafe();
            }
            else
            {
                return ((write_set_flags_ & F_PA_UNSAFE) == 0);
            }
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


        explicit
        TrxHandle(int                 version   = -1,
                  const wsrep_uuid_t& source_id = WSREP_UUID_UNDEFINED,
                  wsrep_conn_id_t     conn_id   = -1,
                  wsrep_trx_id_t      trx_id    = -1,
                  bool                local     = false)
            :
            version_           (version),
            source_id_         (source_id),
            conn_id_           (conn_id),
            trx_id_            (trx_id),
            local_             (local),
            mutex_             (),
            write_set_collection_(working_dir),
            state_             (&trans_map_, S_EXECUTING),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            depends_seqno_     (WSREP_SEQNO_UNDEFINED),
            refcnt_            (1),
            write_set_         (version),
            write_set_out_     (working_dir + '/' + gu::to_string(trx_id,
                                                                  std::hex)),
            write_set_in_      (),
            write_set_flags_   (0),
            certified_         (false),
            committed_         (false),
            gcs_handle_        (-1),
            action_            (0),
            timestamp_         (gu_time_calendar()),
            mac_               (),
            annotation_        (),
            write_set_buffer_  (0, 0),
            cert_keys_         ()
        { }

        void lock()   const { mutex_.lock();   }
        void unlock() const { mutex_.unlock(); }

        int  version()     const { return version_; }
        bool new_version() const { return version() < WS_NG_VERSION; }

        const wsrep_uuid_t& source_id() const { return source_id_; }
        wsrep_trx_id_t      trx_id()    const { return trx_id_;    }
        wsrep_conn_id_t     conn_id()   const { return conn_id_;   }

        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }

        bool is_local()     const { return local_; }
        bool is_certified() const { return certified_; }
        void mark_certified() { certified_ = true; }

        bool is_committed() const { return committed_; }
        void mark_committed() { committed_ = true; }

        void set_received (const void*   action,
                           wsrep_seqno_t seqno_l,
                           wsrep_seqno_t seqno_g)
        {
            action_       = action;
            local_seqno_  = seqno_l;
            global_seqno_ = seqno_g;
        }

        void set_last_seen_seqno(wsrep_seqno_t last_seen_seqno)
        {
            assert (!new_version());
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

        int flags() const { return write_set_flags_; }
        void set_flags(int flags)
        {
            write_set_flags_ = flags;

            if (new_version())
            {
                uint16_t ws_flags(flags & 0x07);
                if (flags & F_ISOLATION) ws_flags |= WriteSetNG::F_TOI;
                if (flags & F_PA_UNSAFE) ws_flags |= WriteSetNG::F_PA_UNSAFE;
                write_set_out_.set_flags(ws_flags);
            }
        }

        void append_key(const KeyData& key)
        {
            /*! protection against protocol change during trx lifetime */
            if (key.proto_ver != version_)
            {
                gu_throw_error(EINVAL)
                    << "key version '"
                    << key.proto_ver
                    << "' does not match to trx version' "
                    << version_ << "'";
            }

            if (new_version())
            {
                write_set_out_.append_key(key);
            }
            else
            {
                write_set_.append_key(key);
            }
        }

        void append_data(const void* data, const size_t data_len, bool store)
        {
            if (new_version())
            {
                write_set_out_.append_data(data, data_len, store);
            }
            else
            {
                write_set_.append_data(data, data_len);
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

        const MappedBuffer& write_set_collection() const
        {
            return write_set_collection_;
        }

        void set_write_set_buffer(const gu::byte_t* buf, size_t buf_len)
        {
            write_set_buffer_.first = buf;
            write_set_buffer_.second = buf_len;
        }

        std::pair<const gu::byte_t*, size_t>
        write_set_buffer() const
        {
            // If external write set buffer location not specified,
            // return location from write_set_colletion_. This is still
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
                return write_set_out_.is_empty();
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
                gu::Buffer buf(serial_size());
                (void)serialize(&buf[0], buf.size(), 0);
                append_write_set(buf);
                write_set_.clear();
            }
        }

        void clear()
        {
            if (new_version()) { assert(0); return; }

            write_set_.clear();
            write_set_collection_.clear();
        }

        void   ref()   { ++refcnt_; }
        void   unref() { if (refcnt_.sub_and_fetch(1) == 0) delete this; }
        size_t refcnt() const { return refcnt_(); }

        WriteSetOut& write_set_out() { return write_set_out_; }
        WriteSetIn&  write_set_in () { return write_set_in_;  }

    private:

        ~TrxHandle() { }
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        int version_;
        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        bool                   local_;
        mutable gu::Mutex      mutex_;
        MappedBuffer           write_set_collection_;
        FSM<State, Transition> state_;
        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          depends_seqno_;
        gu::Atomic<size_t>     refcnt_;
        WriteSet               write_set_;
        WriteSetOut            write_set_out_;
        WriteSetIn             write_set_in_;
        int                    write_set_flags_;
        bool                   certified_;
        bool                   committed_;
        long                   gcs_handle_;
        const void*            action_;
        int64_t                timestamp_;
        Mac                    mac_;
        gu::Buffer             annotation_;

        // Write set buffer location if stored outside TrxHandle.
        std::pair<const gu::byte_t*, size_t> write_set_buffer_;

        //
        friend class Wsdb;
        friend class Certification;
//        typedef std::list<std::pair<KeyEntry*, std::pair<bool, bool> > > CertKeySet;
public:
        typedef gu::UnorderedMap<KeyEntryOS*,
                                 std::pair<bool, bool>,
                                 KeyEntryPtrHash,
                                 KeyEntryPtrEqualAll> CertKeySet;

        size_t serial_size() const;
        size_t serialize  (gu::byte_t* buf, size_t buflen, size_t offset) const;
        size_t unserialize(const gu::byte_t* buf, size_t buflen, size_t offset);

private:
        CertKeySet cert_keys_;

        friend std::ostream& operator<<(std::ostream& os, const TrxHandle& trx);
    };

    std::ostream& operator<<(std::ostream& os, TrxHandle::State s);

    class TrxHandleLock
    {
    public:
        TrxHandleLock(TrxHandle& trx) : trx_(trx) { trx_.lock(); }
        ~TrxHandleLock() { trx_.unlock(); }
    private:
        TrxHandle& trx_;
    };

    template <typename T>
    class Unref2nd
    {
    public:
        void operator()(T& t) const { t.second->unref(); }
    };

}

#endif // GALERA_TRX_HANDLE_HPP
