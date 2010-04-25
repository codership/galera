//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WSDB_WRITE_SET_HPP
#define GALERA_WSDB_WRITE_SET_HPP

extern "C"
{
#include "wsdb_api.h"
}

#include <time.h>

namespace galera
{
    class WsdbWriteSet : public WriteSet
    {
    public:
        WsdbWriteSet(wsrep_trx_id_t trx_id = -1) 
            : 
            trx_id_(trx_id),
            write_set_(0),
            rbr_()
        { }


        void append_query(const void* query, 
                          size_t query_len,
                          time_t t = time(0), 
                          uint32_t rnd = 0)
        {
            if (wsdb_append_query(trx_id_, 
                                  const_cast<char*>(reinterpret_cast<const char*>(query)),
                                  t, rnd) != WSDB_OK)
            {
                gu_throw_fatal;
            }
        }
        
        void append_row_key(const void* dbtable, 
                            size_t dbtable_len,
                            const void* key, 
                            size_t key_len,
                            int action)
        {
            struct wsdb_key_rec   wsdb_key;
            struct wsdb_table_key table_key;
            struct wsdb_key_part  key_part;
            char wsdb_action;
            wsdb_key.key             = &table_key;
            table_key.key_part_count = 1;
            table_key.key_parts      = &key_part;
            key_part.type            = WSDB_TYPE_VOID;
            
            /* assign key info */
            wsdb_key.dbtable     = (char*)dbtable;
            wsdb_key.dbtable_len = dbtable_len;
            key_part.length      = key_len;
            key_part.data        = (uint8_t*)key;
            
            switch (action) {
            case WSREP_UPDATE: wsdb_action=WSDB_ACTION_UPDATE; break;
            case WSREP_DELETE: wsdb_action=WSDB_ACTION_DELETE; break;
            case WSREP_INSERT: wsdb_action=WSDB_ACTION_INSERT; break;
            default:
                gu_throw_fatal; throw;
            }
            
            switch(wsdb_append_row_key(trx_id_, &wsdb_key, wsdb_action)) {
            case WSDB_OK:  
                return;
            default: 
                gu_throw_fatal; throw;
            }
        }


        enum wsdb_ws_type get_type() const
        {
            assert(write_set_ != 0);
            return write_set_->type;
        }

        enum wsdb_ws_level get_level() const
        {
            assert(write_set_ != 0);
            return write_set_->level;
        }

        wsrep_seqno_t get_last_seen_trx() const
        {
            assert(write_set_ != 0);
            return write_set_->last_seen_trx;
        }

        const gu::Buffer& get_rbr() const
        {
            return rbr_;
        }

        bool empty() const
        {
            return rbr_.empty();
        }

        void serialize(gu::Buffer& buf) const
        {
            XDR xdrs;
            size_t data_max(xdr_sizeof((xdrproc_t)xdr_wsdb_write_set, 
                                       (void *)write_set_) + 1);
            buf.resize(data_max);
            xdrmem_create(&xdrs, (char*)&buf[0], data_max, XDR_ENCODE);
            if (!xdr_wsdb_write_set(&xdrs, write_set_)) 
            {
                gu_throw_fatal << "xdr failed";
            }
        }

    private:
        friend class WsdbTrxHandle;
        WsdbWriteSet(const WsdbWriteSet&);
        void operator=(const WsdbWriteSet&);

        wsrep_trx_id_t trx_id_;
        struct wsdb_write_set* write_set_;
        gu::Buffer rbr_;
    };
}

#endif // GALERA_WSDB_WRITE_SET_HPP
