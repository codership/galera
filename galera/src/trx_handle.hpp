
#ifndef GALERA_TRX_HANDLE_HPP
#define GALERA_TRX_HANDLE_HPP

#include "wsrep_api.h"
extern "C"
{
#include "wsdb_api.h"
}
#include "gu_mutex.hpp"
#include <boost/shared_ptr.hpp>

namespace galera
{
    class TrxHandle
    {
    public:
        TrxHandle(wsrep_trx_id_t id) : 
            id_(id), 
            mutex_(),
            seqno_l_(WSREP_SEQNO_UNDEFINED),
            seqno_g_(WSREP_SEQNO_UNDEFINED),
            state_(WSDB_TRX_VOID),
            position_(WSDB_TRX_POS_VOID),
            applier_(0),
            applier_ctx_(0),
            write_set_(0)
        { }
        virtual ~TrxHandle() { }
        void lock() { mutex_.lock(); }
        void unlock() { mutex_.unlock(); }
        virtual void assign_local_seqno(wsrep_seqno_t seqno_l);
        virtual wsrep_seqno_t get_local_seqno() const;
        virtual void assign_global_seqno(wsrep_seqno_t seqno_g);
        virtual wsrep_seqno_t get_global_seqno() const;
        virtual void assign_state(enum wsdb_trx_state state);
        virtual enum wsdb_trx_state get_state() const;
        virtual void assign_position(enum wsdb_trx_position pos);
        virtual enum wsdb_trx_position get_position() const;
        virtual void assign_applier(void*, void*);
        virtual void* get_applier();
        virtual void* get_applier_ctx();
        virtual void set_committed();
        
        virtual int append_row_key(const void* dbtable, 
                                   size_t dbtable_len,
                                   const void* key, 
                                   size_t key_len,
                                   int action);
        virtual void assign_write_set(struct wsdb_write_set* write_set);
        virtual struct wsdb_write_set* get_write_set(const void* row_buf = 0, 
                                                     size_t row_buf_len = 0);
        virtual void clear();

    private:
        TrxHandle(const TrxHandle&);
        void operator=(const TrxHandle& other);
        wsrep_trx_id_t      id_;
        gu::Mutex           mutex_;
        wsrep_seqno_t       seqno_l_;
        wsrep_seqno_t       seqno_g_;
        enum wsdb_trx_state state_;
        enum wsdb_trx_position position_;
        void* applier_;
        void* applier_ctx_;
        struct wsdb_write_set* write_set_;
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
