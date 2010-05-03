//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "wsdb_wsdb.hpp"
#include "wsdb_write_set.hpp"
#include "wsdb_trx_handle.hpp"


#include "gu_lock.hpp"
#include "gu_throw.hpp"

using namespace std;
using namespace gu;

galera::WsdbWsdb::WsdbWsdb()
    : 
    trx_map_(1 << 10), 
    conn_query_map_(), 
    mutex_() 
{
}

galera::WsdbWsdb::~WsdbWsdb()
{

    log_info << "wsdb trx map usage " << trx_map_.size() 
             << " conn query map usage " << conn_query_map_.size();
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
    for_each(conn_query_map_.begin(), 
             conn_query_map_.end(), Unref2nd<TrxMap::value_type>());
}

ostream& galera::WsdbWsdb::operator<<(ostream& os) const
{
    os << "trx map: ";
    for (TrxMap::const_iterator i = trx_map_.begin(); i != trx_map_.end();
         ++i)
    {
        os << i->first << " ";
    }
    os << "\n conn query map: ";
    for (ConnQueryMap::const_iterator i = conn_query_map_.begin(); 
         i != conn_query_map_.end();
         ++i)
    {
        os << i->first << " ";
    }
    os << "\n";
    return os;
}


galera::TrxHandle*
galera::WsdbWsdb::create_trx(wsrep_trx_id_t trx_id)
{
    pair<TrxMap::iterator, bool> i = trx_map_.insert(
        make_pair(trx_id, 
                  new WsdbTrxHandle(-1, trx_id, true)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::TrxHandle* 
galera::WsdbWsdb::create_conn_query(wsrep_conn_id_t conn_id)
{
    pair<TrxMap::iterator, bool> i = conn_query_map_.insert(
        make_pair(conn_id, new WsdbTrxHandle(conn_id, -1, true)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::TrxHandle*
galera::WsdbWsdb::get_trx(wsrep_trx_id_t trx_id, 
                          bool create)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) == trx_map_.end())
    {
        if (create == true)
        {
            return create_trx(trx_id);
        }
        else
        {
            return 0;
        }
    }
    return i->second;
}

galera::TrxHandle* galera::WsdbWsdb::get_conn_query(wsrep_trx_id_t id, 
                                                    bool create)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = conn_query_map_.find(id)) == conn_query_map_.end())
    {
        if (create == true)
        {
            return create_conn_query(id);
        }
        else
        {
            return 0;
        }
    }
    return i->second;
}


void galera::WsdbWsdb::discard_trx(wsrep_trx_id_t trx_id)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) != trx_map_.end())
    {
        wsdb_delete_local_trx_info(trx_id, &static_cast<WsdbTrxHandle*>(i->second)->trx_info_);
        i->second->unref();
        trx_map_.erase(i);

    }
}


void galera::WsdbWsdb::discard_conn_query(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    ConnQueryMap::iterator i;
    if ((i = conn_query_map_.find(conn_id)) != conn_query_map_.end())
    {
        i->second->unref();
        conn_query_map_.erase(i);
    }
    wsdb_conn_reset_seqno(conn_id);
}

void galera::WsdbWsdb::discard_conn(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    wsdb_store_set_database(conn_id, 0, 0);
    ConnQueryMap::iterator i;
    if ((i = conn_query_map_.find(conn_id)) != conn_query_map_.end())
    {
        i->second->unref();
        conn_query_map_.erase(i);
    }
}

void galera::WsdbWsdb::create_write_set(TrxHandle* trx,
                                        const void* rbr_data,
                                        size_t rbr_data_len)
{
    WsdbTrxHandle* wsdb_trx(static_cast<WsdbTrxHandle*>(trx));
    if (wsdb_trx->write_set_ != 0)
    {
        assert(wsdb_trx->write_set_->get_type() == WSDB_WS_TYPE_CONN);
    }
    else
    {
        struct wsdb_write_set* ws(wsdb_get_write_set(trx->get_trx_id(),
                                                     trx->get_conn_id(),
                                                     (char*)rbr_data,
                                                     rbr_data_len,
                                                     &wsdb_trx->trx_info_));
        if (ws != 0)
        {
            assert(rbr_data == 0 || ws->rbr_buf != 0);
            wsdb_trx->assign_write_set(ws);
        }
        else
        {
            log_warn << "no write set for " << trx->get_trx_id();
        }
    }
}


void galera::WsdbWsdb::append_query(TrxHandle* trx,
                                    const void* query,
                                    size_t query_len, 
                                    time_t t,
                                    uint32_t rnd)
{
    if (wsdb_append_query(trx->get_trx_id(), 
                          const_cast<char*>(reinterpret_cast<const char*>(query)),
                          t, rnd, &static_cast<WsdbTrxHandle*>(trx)->trx_info_) != WSDB_OK)
    {
        gu_throw_fatal;
    }
}

void galera::WsdbWsdb::append_conn_query(TrxHandle* trx,
                                         const void* query,
                                         size_t query_len)
{
    struct wsdb_write_set* ws(wsdb_get_conn_write_set(trx->get_conn_id()));
    if (ws == 0)
    {
        gu_throw_fatal;
    }
    wsdb_set_exec_query(ws, (const char*)query, query_len);
    static_cast<WsdbTrxHandle*>(trx)->assign_write_set(ws);
}

        
void galera::WsdbWsdb::append_row_key(TrxHandle* trx,
                                      const void* dbtable, 
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
    
    switch(wsdb_append_row_key(trx->get_trx_id(), &wsdb_key, wsdb_action,
                               &static_cast<WsdbTrxHandle*>(trx)->trx_info_)) {
    case WSDB_OK:  
        return;
    default: 
        gu_throw_fatal; throw;
    }
}

void galera::WsdbWsdb::set_conn_variable(TrxHandle* trx,
                                         const void* key, size_t key_len,
                                         const void* query, size_t query_len)
{
    int err;
    if ((err = wsdb_store_set_variable(trx->get_conn_id(), (char*)key, key_len, 
                                       (char*)query, query_len)) != WSDB_OK)
    {
        gu_throw_fatal << "set conn variable failed for " 
                       << trx->get_conn_id() << " " << err;
    }
}

void galera::WsdbWsdb::set_conn_database(TrxHandle* trx,
                                         const void* query,
                                         size_t query_len)
{
    int err;
    if ((err = wsdb_store_set_database(trx->get_conn_id(), 
                                       (char*)query, query_len)) != WSDB_OK)
    {
        gu_throw_fatal << "set conn database failed for " 
                       << trx->get_conn_id() << " " << err;
    }
}
