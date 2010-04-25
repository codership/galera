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
        WsdbTrxHandle(wsrep_conn_id_t conn_id, wsrep_trx_id_t trx_id, 
                      bool local) 
            :
            TrxHandle(conn_id, trx_id, local)
        { }

        void assign_write_set(struct wsdb_write_set* ws)
        {
            write_set_ = new WsdbWriteSet(get_id());
            static_cast<WsdbWriteSet*>(write_set_)->write_set_ = ws;
        }

        enum wsdb_trx_state get_state() const
        {
            wsdb_trx_info_t info;
            wsdb_get_trx_info(get_id(), &info);
            return info.state;
        }
 
        void assign_seqnos(wsrep_seqno_t seqno_l, wsrep_seqno_t seqno_g)
        {
            assert(write_set_ != 0);
            wsdb_assign_trx_seqno(get_id(), seqno_l, seqno_g, 
                                  get_state(), 
                                  static_cast<WsdbWriteSet*>(write_set_)->write_set_);
        }

        wsrep_seqno_t get_local_seqno() const
        {
            wsdb_trx_info_t info;
            wsdb_get_trx_info(get_id(), &info);
            return info.seqno_l;
        }
        
        wsrep_seqno_t get_global_seqno() const
        {
            if (write_set_ == 0)
            {
                return WSREP_SEQNO_UNDEFINED;
            }
            return static_cast<WsdbWriteSet*>(write_set_)->write_set_->trx_seqno;
        }
        
        void assign_state(enum wsdb_trx_state state)
        {
            wsdb_assign_trx_state(get_id(), state);
        }

        void assign_position(enum wsdb_trx_position position)
        {
            wsdb_assign_trx_pos(get_id(), position);
        }

        enum wsdb_trx_position get_position() const 
        {
            wsdb_trx_info_t info;
            wsdb_get_trx_info(get_id(), &info);
            return info.position;
        }
        void clear()
        {
            WsdbWriteSet* ws(static_cast<WsdbWriteSet*>(write_set_));
            if (ws != 0 && ws->write_set_ != 0)
            {
                XDR xdrs;
                /* free xdr objects */
                xdrs.x_op = XDR_FREE;
                xdr_wsdb_write_set(&xdrs, ws->write_set_);
                gu_free(ws->write_set_);
                ws->write_set_ = 0;
            }
        }

   };
}

#endif // GALERA_WSDB_TRX_HANDLE_HPP
