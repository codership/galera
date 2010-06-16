//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "write_set.hpp"
#include "mapped_buffer.hpp"

#include "wsrep_api.h"
#include "wsdb_api.h"

#include "gu_mutex.hpp"

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

        TrxHandle(const wsrep_uuid_t& source_id = WSREP_UUID_UNDEFINED,
                  wsrep_conn_id_t     conn_id   = -1,
                  wsrep_trx_id_t      trx_id    = -1,
                  bool                local     = false)
            :
            source_id_         (source_id),
            conn_id_           (conn_id),
            trx_id_            (trx_id),
            local_             (local),
            mutex_             (),
            write_set_collection_(working_dir),
            state_             (WSDB_TRX_VOID),
            position_          (WSDB_TRX_POS_VOID),
            local_seqno_       (WSREP_SEQNO_UNDEFINED),
            global_seqno_      (WSREP_SEQNO_UNDEFINED),
            last_seen_seqno_   (WSREP_SEQNO_UNDEFINED),
            last_depends_seqno_(WSREP_SEQNO_UNDEFINED),
            refcnt_            (1),
            write_set_         (),
            write_set_flags_   (0),
            write_set_type_    (),
            certified_         (false),
            committed_         (false),
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

        void set_state(enum wsdb_trx_state state)
        {
            state_ = state;
        }

        void set_position(enum wsdb_trx_position pos)
        {
            position_ = pos;
        }

        enum wsdb_trx_state state() const
        {
            if (is_local() == true)
            {
                return state_;
            }
            else
            {
                gu_throw_fatal << "Internal error: only local trx has state";
                throw;
            }
        }

        wsrep_seqno_t local_seqno() const { return local_seqno_; }

        wsrep_seqno_t global_seqno() const { return global_seqno_; }

        wsrep_seqno_t last_seen_seqno() const { return last_seen_seqno_; }

        wsrep_seqno_t last_depends_seqno() const { return last_depends_seqno_; }

        enum wsdb_trx_position position() const { return position_; }

        void set_flags(int flags) { write_set_flags_ = flags; }
        int flags() const { return write_set_flags_; }

        void set_write_set_type(enum wsdb_ws_type type)
        {
            write_set_type_ = type;
        }

        enum wsdb_ws_type write_set_type() const { return write_set_type_; }


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

        void clear()
        {
            write_set_.clear();
            write_set_collection_.clear();
        }

        void   ref()          { ++refcnt_; }
        void   unref()        { --refcnt_; if (refcnt_ == 0) delete this; }
        size_t refcnt() const { return refcnt_; }

//        std::ostream& operator<<(std::ostream& os) const;

    private:

        ~TrxHandle() { }
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        bool                   local_;
        mutable gu::Mutex      mutex_;
        MappedBuffer           write_set_collection_;
        enum wsdb_trx_state    state_;
        enum wsdb_trx_position position_;
        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_seen_seqno_;
        wsrep_seqno_t          last_depends_seqno_;
        size_t                 refcnt_;
        WriteSet               write_set_;
        int                    write_set_flags_;
        enum wsdb_ws_type      write_set_type_;

        bool                   certified_;
        bool                   committed_;

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



    class TrxHandleLock
    {
    public:
        TrxHandleLock(const TrxHandle& trx) : trx_(trx) { trx_.lock(); }
        ~TrxHandleLock() { trx_.unlock(); }
    private:
        const TrxHandle& trx_;
    };

    template <typename T>
    class Unref2nd
    {
    public:
        void operator()(T& t) const { t.second->unref(); }
    };

}

#endif // GALERA_TRX_HANDLE_HPP
