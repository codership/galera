

#include "galera_wsdb.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"


#include "gu_lock.hpp"
#include "gu_throw.hpp"

using namespace std;
using namespace gu;

galera::GaleraWsdb::GaleraWsdb()
    : 
    trx_map_(), 
    conn_query_map_(), 
    mutex_() 
{
}

galera::GaleraWsdb::~GaleraWsdb()
{
    
    log_info << "wsdb trx map usage " << trx_map_.size() 
             << " conn query map usage " << conn_query_map_.size();
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
    for_each(conn_query_map_.begin(), conn_query_map_.end(), 
             Unref2nd<TrxMap::value_type>());
}

ostream& galera::GaleraWsdb::operator<<(ostream& os) const
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
galera::GaleraWsdb::create_trx(wsrep_trx_id_t trx_id)
{
    pair<TrxMap::iterator, bool> i = trx_map_.insert(
        make_pair(trx_id, 
                  new TrxHandle(-1, trx_id, true)));
    if (i.second == false)
        gu_throw_fatal;
    i.first->second->assign_write_set(new WriteSet(WSDB_WS_TYPE_TRX));
    return i.first->second;
}


galera::TrxHandle*
galera::GaleraWsdb::create_conn_query(wsrep_conn_id_t conn_id)
{
    pair<TrxMap::iterator, bool> i = conn_query_map_.insert(
        make_pair(conn_id, new TrxHandle(conn_id, -1, true)));
    if (i.second == false)
        gu_throw_fatal;
    i.first->second->assign_write_set(new WriteSet(WSDB_WS_TYPE_CONN));
    return i.first->second;
}


galera::TrxHandle*
galera::GaleraWsdb::get_trx(wsrep_trx_id_t trx_id, 
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

galera::TrxHandle* galera::GaleraWsdb::get_conn_query(wsrep_trx_id_t id, 
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


void galera::GaleraWsdb::discard_trx(wsrep_trx_id_t trx_id)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) != trx_map_.end())
    {
        i->second->unref();
        trx_map_.erase(i);
    }
}


void galera::GaleraWsdb::discard_conn_query(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    ConnQueryMap::iterator i;
    if ((i = conn_query_map_.find(conn_id)) != conn_query_map_.end())
    {
        i->second->unref();
        conn_query_map_.erase(i);
    }
}

void galera::GaleraWsdb::discard_conn(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    ConnQueryMap::iterator i;
    if ((i = conn_query_map_.find(conn_id)) != conn_query_map_.end())
    {
        conn_query_map_.erase(i);
    }
}

void galera::GaleraWsdb::create_write_set(TrxHandle* trx,
                                          const void* rbr_data,
                                          size_t rbr_data_len)
{
    if (rbr_data != 0 && rbr_data_len > 0)
    {
        trx->get_write_set().assign_rbr(rbr_data, rbr_data_len);
    }
}


void galera::GaleraWsdb::append_query(TrxHandle* trx,
                                      const void* query,
                                      size_t query_len, 
                                      time_t t,
                                      uint32_t rnd)
{
    trx->get_write_set().append_query(query, query_len);
}


void galera::GaleraWsdb::append_conn_query(TrxHandle* trx,
                                           const void* query,
                                           size_t query_len)
{
    trx->get_write_set().append_query(query, query_len);
}


void galera::GaleraWsdb::append_row_key(TrxHandle* trx,
                                        const void* dbtable, 
                                        size_t dbtable_len,
                                        const void* key, 
                                        size_t key_len,
                                        int action)
{
    trx->get_write_set().append_row_key(dbtable, dbtable_len, key, key_len, action);
}


void galera::GaleraWsdb::set_conn_variable(TrxHandle* trx,
                                           const void* key, size_t key_len,
                                           const void* query, size_t query_len)
{
    // TODO
}


void galera::GaleraWsdb::set_conn_database(TrxHandle* trx,
                                           const void* query,
                                           size_t query_len)
{
    trx->get_write_set().append_query(query, query_len);
}
