//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "write_set.hpp"

#include "wsrep_api.h"
#include "wsdb_api.h"

#include "gu_mutex.hpp"
#include <boost/shared_ptr.hpp>

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
            write_set_(0)
        { }
        virtual ~TrxHandle() { delete write_set_; }

        void lock() { mutex_.lock(); }
        void unlock() { mutex_.unlock(); }
        
        virtual wsrep_trx_id_t get_id() const { return trx_id_; }
        virtual wsrep_conn_id_t get_conn_id() const { return conn_id_; }
        virtual bool is_local() const { return local_; }
        
        virtual void assign_seqnos(wsrep_seqno_t seqno_l, 
                                   wsrep_seqno_t seqno_g) = 0;
        virtual wsrep_seqno_t get_local_seqno() const = 0;
        virtual wsrep_seqno_t get_global_seqno() const = 0;
        virtual void assign_state(enum wsdb_trx_state state) = 0;
        virtual enum wsdb_trx_state get_state() const = 0;
        virtual void assign_position(enum wsdb_trx_position pos) = 0;
        virtual enum wsdb_trx_position get_position() const = 0;
        
        virtual const WriteSet& get_write_set() const 
        {
            assert(write_set_ != 0);
            return *write_set_; 
        };
        virtual void clear() = 0;
        
    private:
        
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);
        wsrep_conn_id_t     conn_id_;
        wsrep_trx_id_t      trx_id_;
        bool                local_;
        gu::Mutex           mutex_;
    protected:
        WriteSet* write_set_;
    };
    
    
    typedef boost::shared_ptr<TrxHandle> TrxHandlePtr;

    
    class TrxHandleLock
    {
    public:
        TrxHandleLock(TrxHandlePtr& trx) : trx_(trx) { trx_->lock(); }
        ~TrxHandleLock() { trx_->unlock(); }
    private:
        TrxHandlePtr& trx_;
    };
    
}

#endif // GALERA_TRX_HANDLE_HPP
