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

namespace galera
{
    
    class RowKeyEntry; // Forward declaration

    static const std::string working_dir = "/tmp";
        
    class TrxHandle
    {
    public:
        TrxHandle(wsrep_conn_id_t conn_id,
                  wsrep_trx_id_t trx_id, 
                  bool local) : 
            source_id_(),
            conn_id_(conn_id),
            trx_id_(trx_id),
            local_(local),
            mutex_(),
            write_set_(0),
            write_set_collection_(working_dir),
            state_(WSDB_TRX_VOID),
            position_(WSDB_TRX_POS_VOID),
            local_seqno_(WSREP_SEQNO_UNDEFINED),
            global_seqno_(WSREP_SEQNO_UNDEFINED),
            last_depends_seqno_(WSREP_SEQNO_UNDEFINED),
            refcnt_(1),
            cert_keys_()
        { }
        
        void lock() { mutex_.lock(); }
        void unlock() { mutex_.unlock(); }
        
        void assign_source_id(const wsrep_uuid_t& source_id)
        {
            source_id_ = source_id;
        }

        const wsrep_uuid_t& get_source_id() const
        {
            return source_id_;
        }
        
        wsrep_trx_id_t get_trx_id() const { return trx_id_; }
        void assign_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }
        wsrep_conn_id_t get_conn_id() const { return conn_id_; }
        bool is_local() const { return local_; }
        
        void assign_seqnos(wsrep_seqno_t seqno_l, 
                           wsrep_seqno_t seqno_g)
        {
            local_seqno_ = seqno_l; 
            global_seqno_ = seqno_g; 
        }
        
        void assign_last_depends_seqno(wsrep_seqno_t seqno_lt)
        {
            last_depends_seqno_ = seqno_lt;
        }
        
        void assign_state(enum wsdb_trx_state state) 
        { 
            state_ = state; 
        }
        
        void assign_position(enum wsdb_trx_position pos)
        { 
            position_ = pos; 
        }
        
        
        enum wsdb_trx_state get_state() const
        {
            if (is_local() == true)
            {
                return state_;
            }
            else
            {
                gu_throw_fatal << "only local trx has state";
                throw;
            }
        }
        
        wsrep_seqno_t get_local_seqno() const { return local_seqno_; }
        wsrep_seqno_t get_global_seqno() const { return global_seqno_; }
        wsrep_seqno_t get_last_depends_seqno() const { return last_depends_seqno_; }
        enum wsdb_trx_position get_position() const { return position_; }
        
        

        void assign_write_set(WriteSet* ws) 
        {
            assert(write_set_ == 0);
            write_set_ = ws;
            if (ws != 0)
            {
                source_id_ = ws->get_source_id();
                trx_id_ = ws->get_trx_id();
            }
        }
        
        void append_write_set(const gu::Buffer& ws)
        {
            const size_t offset(write_set_collection_.size());
            write_set_collection_.resize(offset + ws.size());
            std::copy(ws.begin(), ws.end(), &write_set_collection_[0] + offset);
        }

        WriteSet& get_write_set()
        {
            assert(write_set_ != 0);
            return* write_set_;
        }

        const WriteSet& get_write_set() const 
        {
            assert(write_set_ != 0);
            return *write_set_; 
        }

        const MappedBuffer& get_write_set_collection() const
        {
            return write_set_collection_;
        }

        void clear() 
        { 
            if (write_set_ != 0) write_set_->clear(); 
            write_set_collection_.clear();
        }
        
        void ref() { ++refcnt_; }
        void unref() { --refcnt_; if (refcnt_ == 0) delete this; }
        size_t refcnt() const { return refcnt_; }
    private:
        virtual ~TrxHandle() { delete write_set_; write_set_ = 0; }        
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);

        wsrep_uuid_t           source_id_;
        wsrep_conn_id_t        conn_id_;
        wsrep_trx_id_t         trx_id_;
        bool                   local_;
        gu::Mutex              mutex_;
        WriteSet*              write_set_;
        MappedBuffer           write_set_collection_;
        enum wsdb_trx_state    state_;
        enum wsdb_trx_position position_;
        wsrep_seqno_t          local_seqno_;
        wsrep_seqno_t          global_seqno_;
        wsrep_seqno_t          last_depends_seqno_;
        size_t                 refcnt_;
        
        // 
        friend class GaleraCertification;
        std::deque<RowKeyEntry*> cert_keys_;
    };
    
    
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
