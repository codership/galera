/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "wsdb.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"


galera::Wsdb::Wsdb()
    :
    trx_map_(),
    conn_map_(),
    mutex_()
{ }


galera::Wsdb::~Wsdb()
{
    log_info << "wsdb trx map usage " << trx_map_.size()
             << " conn query map usage " << conn_map_.size();
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
}


std::ostream& galera::Wsdb::operator<<(std::ostream& os) const
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
galera::Wsdb::create_trx(int                 version,
                         const wsrep_uuid_t& source_id,
                         wsrep_trx_id_t      trx_id)
{
    std::pair<TrxMap::iterator, bool> i = trx_map_.insert(
        std::make_pair(trx_id,
                       new TrxHandle(version, source_id, -1, trx_id, true)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::Wsdb::Conn&
galera::Wsdb::create_conn(wsrep_conn_id_t conn_id)
{
    std::pair<ConnMap::iterator, bool> i = conn_map_.insert(
        std::make_pair(conn_id, Conn(conn_id)));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::TrxHandle*
galera::Wsdb::get_trx(int                 version,
                      const wsrep_uuid_t& source_id,
                      wsrep_trx_id_t      trx_id,
                      bool                create)
{
    gu::Lock lock(mutex_);
    TrxMap::iterator i;
    TrxHandle* retval;
    if ((i = trx_map_.find(trx_id)) == trx_map_.end())
    {
        if (create == true)
        {
            retval = create_trx(version, source_id, trx_id);
        }
        else
        {
            retval = 0;
        }
    }
    else
    {
        retval = i->second;
    }
    if (retval != 0)
    {
        retval->ref();
    }
    return retval;
}


void galera::Wsdb::unref_trx(TrxHandle* trx)
{
    gu::Lock lock(mutex_);
    assert(trx->refcnt() > 1);
    trx->unref();

}


galera::TrxHandle*
galera::Wsdb::get_conn_query(int                 version,
                             const wsrep_uuid_t& source_id,
                             wsrep_trx_id_t      conn_id,
                             bool                create)
{
    gu::Lock lock(mutex_);
    ConnMap::iterator i;

    if ((i = conn_map_.find(conn_id)) == conn_map_.end())
    {
        if (create == true)
        {
            Conn& conn(create_conn(conn_id));
            TrxHandle* trx(new TrxHandle(
                               version, source_id, conn_id, -1, true));
            conn.assign_trx(trx);
            return trx;
        }
        else
        {
            return 0;
        }
    }

    if (i->second.get_trx() == 0 && create == true)
    {
        TrxHandle* trx(new TrxHandle(version, source_id, conn_id, -1, true));
        i->second.assign_trx(trx);
    }

    return i->second.get_trx();
}


void galera::Wsdb::discard_trx(wsrep_trx_id_t trx_id)
{
    gu::Lock lock(mutex_);
    TrxMap::iterator i;
    if ((i = trx_map_.find(trx_id)) != trx_map_.end())
    {
        i->second->unref();
        trx_map_.erase(i);
    }
}


void galera::Wsdb::discard_conn_query(wsrep_conn_id_t conn_id)
{
    gu::Lock lock(mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        i->second.assign_trx(0);
    }
}

void galera::Wsdb::discard_conn(wsrep_conn_id_t conn_id)
{
    gu::Lock lock(mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        conn_map_.erase(i);
    }
}
