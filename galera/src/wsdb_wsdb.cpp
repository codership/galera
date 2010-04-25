//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "wsdb_wsdb.hpp"
#include "wsdb_write_set.hpp"
#include "wsdb_trx_handle.hpp"


#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <boost/unordered_map.hpp>

using namespace std;
using namespace gu;


galera::TrxHandlePtr galera::WsdbWsdb::create_trx(wsrep_trx_id_t trx_id)
{
    Lock lock(mutex_);
    pair<TrxMap::iterator, bool> i = trx_map_.insert(
        make_pair(trx_id, TrxHandlePtr(new WsdbTrxHandle(-1, trx_id, true))));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}

galera::TrxHandlePtr galera::WsdbWsdb::create_conn_query(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    pair<TrxMap::iterator, bool> i = trx_map_.insert(
        make_pair(conn_id, TrxHandlePtr(new WsdbTrxHandle(conn_id, -1, true))));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::TrxHandlePtr galera::WsdbWsdb::get_trx(wsrep_trx_id_t id, bool create)
{
    Lock lock(mutex_);
    
    TrxMap::iterator i;
    if ((i = trx_map_.find(id)) == trx_map_.end())
    {
        if (create == true)
        {
            return create_trx(id);
        }
        else
        {
            return TrxHandlePtr();
        }
    }
    return i->second;
}

galera::TrxHandlePtr galera::WsdbWsdb::get_conn_query(wsrep_trx_id_t id, 
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
            return TrxHandlePtr();
        }
    }
    return i->second;
}


void galera::WsdbWsdb::discard_trx(wsrep_trx_id_t id)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(id)) != trx_map_.end())
    {
        trx_map_.erase(i);
    }
}


void galera::WsdbWsdb::create_write_set(TrxHandlePtr& trx,
                                        const void* rbr_data,
                                        size_t rbr_data_len)
{
    WsdbTrxHandle* wsdb_trx(static_cast<WsdbTrxHandle*>(trx.get()));
    assert(wsdb_trx->write_set_ == 0);
    struct wsdb_write_set* ws(wsdb_get_write_set(trx->get_id(),
                                                 -1,
                                                 (char*)rbr_data,
                                                 rbr_data_len));
    if (ws != 0)
    {
        wsdb_trx->assign_write_set(ws);
    }
}




