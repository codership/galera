/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "wsdb.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

using namespace std;
using namespace gu;

galera::Wsdb::Wsdb()
    :
    trx_map_(),
    conn_map_(),
    mutex_()
{
}

galera::Wsdb::~Wsdb()
{
    log_info << "wsdb trx map usage " << trx_map_.size()
             << " conn query map usage " << conn_map_.size();
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
}

ostream& galera::Wsdb::operator<<(ostream& os) const
{
    os << "trx map: ";
    for (TrxMap::const_iterator i = trx_map_.begin(); i != trx_map_.end();
         ++i)
    {
        os << i->first << " ";
    }
    os << "\n conn query map: ";
    for (ConnMap::const_iterator i = conn_map_.begin();
         i != conn_map_.end();
         ++i)
    {
        os << i->first << " ";
    }
    os << "\n";
    return os;
}

galera::TrxHandle*
galera::Wsdb::create_trx(const wsrep_uuid_t& source_id,
                               wsrep_trx_id_t trx_id)
{
    pair<TrxMap::iterator, bool> i = trx_map_.insert(
        make_pair(trx_id,
                  new TrxHandle(source_id, -1, trx_id, true)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::Wsdb::Conn&
galera::Wsdb::create_conn(wsrep_conn_id_t conn_id)
{
    pair<ConnMap::iterator, bool> i = conn_map_.insert(
        make_pair(conn_id, Conn(conn_id)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}

galera::TrxHandle*
galera::Wsdb::get_trx(const wsrep_uuid_t& source_id,
                            wsrep_trx_id_t trx_id,
                            bool create)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) == trx_map_.end())
    {
        if (create == true)
        {
            return create_trx(source_id, trx_id);
        }
        else
        {
            return 0;
        }
    }
    return i->second;
}

galera::TrxHandle*
galera::Wsdb::get_conn_query(const wsrep_uuid_t& source_id,
                                   wsrep_trx_id_t conn_id,
                                   bool create)
{
    Lock lock(mutex_);
    ConnMap::iterator i;

    if ((i = conn_map_.find(conn_id)) == conn_map_.end())
    {
        if (create == true)
        {
            Conn& conn(create_conn(conn_id));
            TrxHandle* trx(new TrxHandle(source_id, conn_id, -1, true));
            conn.assign_trx(trx);
            return trx;
        }
        else
        {
            return 0;
        }
    }
    if (i->second.get_trx() == 0)
    {
        TrxHandle* trx(new TrxHandle(source_id, conn_id, -1, true));
        i->second.assign_trx(trx);
    }
    return i->second.get_trx();
}


void galera::Wsdb::discard_trx(wsrep_trx_id_t trx_id)
{
    Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) != trx_map_.end())
    {
        i->second->unref();
        trx_map_.erase(i);
    }
}


void galera::Wsdb::discard_conn_query(wsrep_conn_id_t conn_id)
{

    Lock lock(mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        i->second.assign_trx(0);
    }
}

void galera::Wsdb::discard_conn(wsrep_conn_id_t conn_id)
{
    Lock lock(mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        conn_map_.erase(i);
    }
}

void galera::Wsdb::create_write_set(TrxHandle* trx,
                                          const void* rbr_data,
                                          size_t rbr_data_len)
{

    if (rbr_data != 0 && rbr_data_len > 0)
    {
        trx->write_set_.append_data(rbr_data, rbr_data_len);
    }

    if (trx->write_set().get_queries().empty() == false)
    {
        ConnMap::const_iterator i(conn_map_.find(trx->conn_id()));
        if (i != conn_map_.end())
        {
            const Conn& conn(i->second);
            if (conn.get_default_db().get_query().size() > 0)
            {
                trx->write_set_.prepend_query(conn.get_default_db());
            }
        }
    }
}


void galera::Wsdb::append_query(TrxHandle* trx,
                                      const void* query,
                                      size_t query_len,
                                      time_t t,
                                      uint32_t rnd)
{
    trx->write_set_.append_query(query, query_len, t, rnd);
}


void galera::Wsdb::append_conn_query(TrxHandle* trx,
                                           const void* query,
                                           size_t query_len)
{
    trx->write_set_.append_query(query, query_len);
}


void galera::Wsdb::append_row_key(TrxHandle* trx,
                                        const void* dbtable,
                                        size_t dbtable_len,
                                        const void* key,
                                        size_t key_len,
                                        int action)
{
    trx->write_set_.append_row_key(dbtable, dbtable_len,
                                    key, key_len, action);
}


void galera::Wsdb::append_data(TrxHandle* trx,
                                     const void* data,
                                     size_t data_len)
{
    trx->write_set_.append_data(data, data_len);
    flush_trx(trx);
}


void galera::Wsdb::set_conn_variable(TrxHandle* trx,
                                           const void* key, size_t key_len,
                                           const void* query, size_t query_len)
{
    // TODO
}


void galera::Wsdb::set_conn_database(TrxHandle* trx,
                                           const void* query,
                                           size_t query_len)
{
    if (trx->conn_id() != static_cast<wsrep_conn_id_t>(-1))
    {
        Conn& conn(conn_map_.find(trx->conn_id())->second);
        conn.assing_default_db(Query(query, query_len));
    }
}


void galera::Wsdb::flush_trx(TrxHandle* trx, bool force)
{
    WriteSet& ws(trx->write_set_);
    if (ws.get_key_buf().size() + ws.get_data().size()
        >= trx_mem_limit_ || force == true)
    {
        Buffer buf(serial_size(ws));
        (void)serialize(ws, &buf[0], buf.size(), 0);
        trx->append_write_set(buf);
        ws.clear();
    }
}
