//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "write_set.hpp"

#include "wsrep_api.h"
#include "wsdb_api.h"

#include "gu_mutex.hpp"

namespace galera
{
    class TrxHandle
    {
    public:
        TrxHandle(wsrep_conn_id_t conn_id,
                  wsrep_trx_id_t trx_id, 
                  bool local) : 
            conn_id_(conn_id),
            trx_id_(trx_id),
            local_(local),
            mutex_(),
            write_set_(0),
            state_(WSDB_TRX_VOID),
            position_(WSDB_TRX_POS_VOID),
            local_seqno_(WSREP_SEQNO_UNDEFINED),
            global_seqno_(WSREP_SEQNO_UNDEFINED),
            refcnt_(1)
        { }
        virtual ~TrxHandle() { delete write_set_; write_set_ = 0; }
        
        void lock() { mutex_.lock(); }
        void unlock() { mutex_.unlock(); }
        
        wsrep_trx_id_t get_trx_id() const { return trx_id_; }
        void assign_conn_id(wsrep_conn_id_t conn_id) { conn_id_ = conn_id; }
        wsrep_conn_id_t get_conn_id() const { return conn_id_; }
        bool is_local() const { return local_; }
        
        virtual void assign_seqnos(wsrep_seqno_t seqno_l, 
                                   wsrep_seqno_t seqno_g)
        { 
            local_seqno_ = seqno_l; 
            global_seqno_ = seqno_g; 
        }

        virtual void assign_state(enum wsdb_trx_state state) 
        { 
            state_ = state; 
        }

        virtual void assign_position(enum wsdb_trx_position pos)
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
        enum wsdb_trx_position get_position() const { return position_; }
        
        virtual void assign_write_set(WriteSet* ws) 
        {
            assert(write_set_ == 0);
            write_set_ = ws;
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
        virtual void clear() { }

        void ref() { ++refcnt_; }
        void unref() { --refcnt_; if (refcnt_ == 0) delete this; }
        
    private:
        
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);
        wsrep_conn_id_t     conn_id_;
        wsrep_trx_id_t      trx_id_;
        bool                local_;
        gu::Mutex           mutex_;
    protected:
        WriteSet* write_set_;
        enum wsdb_trx_state state_;
        enum wsdb_trx_position position_;
        wsrep_seqno_t local_seqno_;
        wsrep_seqno_t global_seqno_;
        size_t refcnt_;
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
