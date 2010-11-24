//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "write_set.hpp"
#include "mapped_buffer.hpp"
#include "fsm.hpp"

#include "wsrep_api.h"
#include "gu_mutex.hpp"
#include "gu_atomic.hpp"

#include <set>

namespace galera
{

    class RowKeyEntry; // Forward declaration

    static const std::string working_dir = "/tmp";

    class TrxHandle
    {
    public:
        enum
        {
            F_COMMIT =   1 << 0,
            F_ROLLBACK = 1 << 1
        };

        typedef enum
        {
            S_EXECUTING,
            S_MUST_ABORT,
            S_ABORTING,
            S_REPLICATING,
            S_REPLICATED,
            S_CERTIFYING,
            S_CERTIFIED,
            S_MUST_CERT_AND_REPLAY,
            S_MUST_REPLAY,
            S_REPLAYING,
            S_REPLAYED,
            S_APPLYING,
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

        explicit
        TrxHandle(const wsrep_uuid_t& source_id = WSREP_UUID_UNDEFINED,
                  wsrep_conn_id_t     conn_id   = -1,
                  wsrep_trx_id_t      trx_id    = -1,
                  bool                local     = false)
            :
            version_           (0),
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
            last_depends_seqno_(WSREP_SEQNO_UNDEFINED),
            refcnt_            (1),
            write_set_         (),
            write_set_flags_   (0),
            certified_         (false),
            committed_         (false),
            gcs_handle_        (-1),
            action_            (0),
            cert_keys_         ()
        { }

        void lock()   const { mutex_.lock();   }
        void unlock() const { mutex_.unlock(); }

        const wsrep_uuid_t& source_id() const { return source_id_; }

        wsrep_trx_id_t trx_id() const { return trx_id_; }
        void set_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }
        wsrep_conn_id_t conn_id() const { return conn_id_; }

        bool is_local() const { return local_; }

        bool is_certified() const { return certified_; }
        void mark_certified() { certified_ = true; }

        bool is_committed() const { return committed_; }
        void mark_committed() { committed_ = true; }

        void set_seqnos(wsrep_seqno_t seqno_l, wsrep_seqno_t seqno_g)
        {
            local_seqno_  = seqno_l;
            global_seqno_ = seqno_g;
        }

        void set_last_seen_seqno(wsrep_seqno_t last_seen_seqno)
        {
            last_seen_seqno_ = last_seen_seqno;
        }

        void set_last_depends_seqno(wsrep_seqno_t seqno_lt)
        {
            last_depends_seqno_ = seqno_lt;
        }

        void set_state(State state)
        {
            state_.shift_to(state);
        }
        State state() const { return state_(); }

        void set_gcs_handle(long gcs_handle) { gcs_handle_ = gcs_handle; }
        long gcs_handle() const { return gcs_handle_; }

        void set_action(void* action)
        {
            action_ = action;
        }
        void* action() const { return action_; }

        wsrep_seqno_t local_seqno() const { return local_seqno_; }

        wsrep_seqno_t global_seqno() const { return global_seqno_; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t last_depends_seqno() const { return last_depends_seqno_; }


        void set_flags(int flags) { write_set_flags_ = flags; }
        int flags() const { return write_set_flags_; }

        void append_statement(const void* stmt,
                              size_t stmt_len,
                              time_t timeval = -1,
                              uint32_t randseed = -1)
        {
            write_set_.append_statement(
                Statement(stmt, stmt_len,timeval, randseed));
        }

        void append_row_id(const void* dbtable, size_t dbtable_len,
                           const void* row_id, size_t row_id_len,
                           int action)
        {
            write_set_.append_row_key(dbtable, dbtable_len,
                                      row_id, row_id_len,
                                      action);
        }

        void append_data(const void* data, const size_t data_len)
        {
            write_set_.append_data(data, data_len);
        }

        const WriteSet& write_set() const { return write_set_; }

        size_t prepare_write_set_collection()
        {
            size_t offset;
            if (write_set_collection_.empty() == true)
            {
                offset = serial_size(*this);
                write_set_collection_.resize(offset);
            }
            else
            {
                offset = write_set_collection_.size();
            }
            (void)serialize(*this, &write_set_collection_[0], offset, 0);
            return offset;
        }

        void append_write_set(const void* data, size_t data_len)
        {
            const size_t offset(prepare_write_set_collection());
            write_set_collection_.resize(offset + data_len);
            std::copy(reinterpret_cast<const gu::byte_t*>(data),
                      reinterpret_cast<const gu::byte_t*>(data) + data_len,
                      &write_set_collection_[0] + offset);
        }

        void append_write_set(const gu::Buffer& ws)
        {
            const size_t offset(prepare_write_set_collection());
            write_set_collection_.resize(offset + ws.size());
            std::copy(ws.begin(), ws.end(), &write_set_collection_[0] + offset);
        }

        const MappedBuffer& write_set_collection() const
        {
            return write_set_collection_;
        }

        bool empty() const
        {
            return (write_set_.empty() == true &&
                    write_set_collection_.size() <= serial_size(*this));
        }

        void flush(size_t mem_limit)
        {
            if (write_set_.get_key_buf().size() + write_set_.get_data().size()
                > mem_limit || mem_limit == 0)
            {
                gu::Buffer buf(serial_size(write_set_));
                (void)serialize(write_set_, &buf[0], buf.size(), 0);
                append_write_set(buf);
                write_set_.clear();
            }
        }

        void clear()
        {
            write_set_.clear();
            write_set_collection_.clear();
        }

        void   ref()          { ++refcnt_; }
        void   unref()        { if (refcnt_.sub_and_fetch(1) == 0) delete this; }
        size_t refcnt() const { return refcnt_(); }

//        std::ostream& operator<<(std::ostream& os) const;

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
        wsrep_seqno_t          last_depends_seqno_;
        gu::Atomic<size_t>     refcnt_;
        WriteSet               write_set_;
        int                    write_set_flags_;
        bool                   certified_;
        bool                   committed_;
        long                   gcs_handle_;
        void*                  action_;

        //
        friend class Wsdb;
        friend class Certification;
        typedef std::set<RowKeyEntry*> CertKeySet;
        CertKeySet cert_keys_;

        friend size_t serialize(const TrxHandle&, gu::byte_t* buf,
                                size_t buflen, size_t offset);
        friend size_t unserialize(const gu::byte_t*, size_t, size_t, TrxHandle&);
        friend size_t serial_size(const TrxHandle&);

        friend std::ostream& operator<<(std::ostream& os, const TrxHandle& trx);
    };

    std::ostream& operator<<(std::ostream& os, TrxHandle::State s);

    size_t serial_size(const TrxHandle&);

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
