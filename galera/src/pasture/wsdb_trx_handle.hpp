//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WSDB_TRX_HANDLE_HPP
#define GALERA_WSDB_TRX_HANDLE_HPP

#include "trx_handle.hpp"
#include "wsdb_write_set.hpp"

#include "gu_mem.h"

namespace galera
{
    class WsdbTrxHandle : public TrxHandle
    {
    public:
        WsdbTrxHandle(wsrep_conn_id_t conn_id, 
                      wsrep_trx_id_t trx_id, 
                      bool local) 
            :
            TrxHandle(conn_id, trx_id, local), trx_info_(0)

        { }

        ~WsdbTrxHandle()
        {
            delete write_set_; write_set_ = 0;
        }

        void assign_write_set(struct wsdb_write_set* ws)
        {
            write_set_ = new WsdbWriteSet(get_trx_id());
            static_cast<WsdbWriteSet*>(write_set_)->write_set_ = ws;
        }
        
        void assign_seqnos(wsrep_seqno_t seqno_l, wsrep_seqno_t seqno_g)
        {
            assert(write_set_ != 0);
            assert(seqno_l >= 0 && seqno_g >= 0);
            if (is_local() == true)
            {
                switch (write_set_->get_type())
                {
                case WSDB_WS_TYPE_TRX:
                    wsdb_assign_trx_seqno(get_trx_id(), seqno_l, seqno_g, 
                                          get_state(), 
                                          static_cast<WsdbWriteSet*>(write_set_)->write_set_, &trx_info_);
                    break;
                case WSDB_WS_TYPE_CONN:
                    wsdb_conn_set_seqno(get_conn_id(), seqno_l, seqno_g);
                    break;
                }
            }
            else
            {
                static_cast<WsdbWriteSet*>(write_set_)->write_set_->trx_seqno = seqno_g;
            }
            // Cache values
            local_seqno_ = seqno_l;
            global_seqno_ = seqno_g;
        }


        
        void assign_state(enum wsdb_trx_state state)
        {
            if (is_local() == true)
            {
                if (state == WSDB_TRX_ABORTING_NONREPL && write_set_ != 0)
                {
                    
                    assert(static_cast<WsdbWriteSet*>(write_set_)->write_set_ != 0);
                    wsdb_deref_seqno(static_cast<WsdbWriteSet*>(write_set_)->write_set_->last_seen_trx);
                }
                wsdb_assign_trx_state(get_trx_id(), state, &trx_info_);
                state_ = state;
            }
            else
            {
                gu_throw_fatal << "not implemented";
                throw;
            }
        }

        void assign_position(enum wsdb_trx_position position)
        {
            if (is_local() == true)
            {
                wsdb_assign_trx_pos(get_trx_id(), position, &trx_info_);
                position_ = position;
            }
            else
            {
                gu_throw_fatal << "not implemented";
                throw;
            }
        }



        void clear()
        {
            int err;
            if ((err = wsdb_delete_local_trx(get_trx_id(), &trx_info_)) != WSDB_OK)
            {
                log_warn << "delete local trx: " << err;
            }
        }
    private:
        WsdbTrxHandle(const WsdbTrxHandle&);
        void operator=(const WsdbTrxHandle&);
        friend class WsdbWsdb;
        friend class WsdbCertification;
        wsdb_trx_handle_t* trx_info_;
   };
}


#endif // GALERA_WSDB_TRX_HANDLE_HPP
