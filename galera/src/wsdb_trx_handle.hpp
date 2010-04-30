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
            TrxHandle(conn_id, trx_id, local),
            state_(WSDB_TRX_VOID),
            position_(WSDB_TRX_POS_VOID),
            local_seqno_(WSREP_SEQNO_UNDEFINED),
            global_seqno_(WSREP_SEQNO_UNDEFINED)
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

        enum wsdb_trx_state get_state() const
        {
            if (is_local() == true)
            {
                return state_;
            }
            else
            {
                gu_throw_fatal << "not implemented";
                throw;
            }
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
                                          static_cast<WsdbWriteSet*>(write_set_)->write_set_);
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

        wsrep_seqno_t get_local_seqno() const
        {
            return local_seqno_;
        }
        
        wsrep_seqno_t get_global_seqno() const
        {
            return global_seqno_;
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
                wsdb_assign_trx_state(get_trx_id(), state);
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
                wsdb_assign_trx_pos(get_trx_id(), position);
                position_ = position;
            }
            else
            {
                gu_throw_fatal << "not implemented";
                throw;
            }
        }

        enum wsdb_trx_position get_position() const 
        {
            return position_;
        }

        void clear()
        {
            delete write_set_; write_set_ = 0;
        }
    private:
        friend class WsdbWsdb;
        enum wsdb_trx_state state_;
        enum wsdb_trx_position position_;
        wsrep_seqno_t local_seqno_;
        wsrep_seqno_t global_seqno_;
   };
}

#endif // GALERA_WSDB_TRX_HANDLE_HPP
