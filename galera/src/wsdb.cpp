/*
 * Copyright (C) 2010-2014 Codership Oy <info@codership.com>
 */

#include "wsdb.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"


void galera::Wsdb::print(std::ostream& os) const
{
    os << "trx map:\n";
    for (galera::Wsdb::TrxMap::const_iterator i = trx_map_.begin();
         i != trx_map_.end();
         ++i)
    {
        os << i->first << " " << *i->second << "\n";
    }
    os << "conn query map:\n";
    for (galera::Wsdb::ConnMap::const_iterator i = conn_map_.begin();
         i != conn_map_.end();
         ++i)
    {
        os << i->first << " ";
    }
    os << "\n";
}


galera::Wsdb::Wsdb()
    :
    trx_pool_  (TrxHandle::LOCAL_STORAGE_SIZE(), 512, "LocalTrxHandle"),
    trx_map_     (),
    conn_trx_map_(),
#ifdef HAVE_PSI_INTERFACE
    trx_mutex_   (WSREP_PFS_INSTR_TAG_WSDB_TRX_MUTEX),
#else
    trx_mutex_   (),
#endif /* HAVE_PSI_INTERFACE */
    conn_map_    (),
#ifdef HAVE_PSI_INTERFACE
    conn_mutex_  (WSREP_PFS_INSTR_TAG_WSDB_CONN_MUTEX)
#else
    conn_mutex_  ()
#endif /* HAVE_PSI_INTERFACE */
{}


galera::Wsdb::~Wsdb()
{
    log_debug << "wsdb trx map usage " << trx_map_.size()
             << " conn query map usage " << conn_map_.size();
    log_debug << trx_pool_;

    // With debug builds just print trx and query maps to stderr
    // and don't clean up to let valgrind etc to detect leaks.
#ifndef NDEBUG
    std::cerr << *this;
#else
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
    for_each(conn_trx_map_.begin(),
             conn_trx_map_.end(),
             Unref2nd<ConnTrxMap::value_type>());
#endif // !NDEBUG
}


inline galera::TrxHandle*
galera::Wsdb::find_trx(wsrep_trx_id_t const trx_id)
{
    gu::Lock lock(trx_mutex_);

    galera::TrxHandle* trx;
    /* trx-id = 0 is safe-guard condition.
    trx-id is generally assigned from thd->query-id
    and query-id default is 0. If background thread
    try to assign set wsrep_next_trx_id before setting
    query-id we will hit the said assert. */
    assert(trx_id != 0);

    if (trx_id != wsrep_trx_id_t(-1))
    {
        /* trx_id is valid and valid ids are unique.
        Search for valid trx_id in trx_id -> trx map. */
        TrxMap::iterator const i(trx_map_.find(trx_id));
        trx = (trx_map_.end() == i ? NULL : i->second);
    }
    else
    {
        /* trx_id is default so search for repsective connection id
        in connection-transaction map. */
        pthread_t const id = pthread_self();
        ConnTrxMap::iterator const i(conn_trx_map_.find(id));
        trx = (conn_trx_map_.end() == i ? NULL : i->second);
    }

    return (trx);
}


inline galera::TrxHandle*
galera::Wsdb::create_trx(const TrxHandle::Params& params,
                         const wsrep_uuid_t&  source_id,
                         wsrep_trx_id_t const trx_id)
{
    TrxHandle* trx(TrxHandle::New(trx_pool_, params, source_id, -1, trx_id));

    gu::Lock lock(trx_mutex_);

    galera::TrxHandle* trx_ref;
    if (trx_id != wsrep_trx_id_t(-1))
    {
        /* trx_id is valid add it to trx-map as valid trx_id is unique
        accross connections. */
        std::pair<TrxMap::iterator, bool> i
            (trx_map_.insert(std::make_pair(trx_id, trx)));
        if (gu_unlikely(i.second == false)) gu_throw_fatal;
        trx_ref = i.first->second;
    }
    else
    {
        /* trx_id is default so add trx object to connection map
        that is maintained based on pthread_id (alias for connection_id). */
         std::pair<ConnTrxMap::iterator, bool> i
             (conn_trx_map_.insert(std::make_pair(pthread_self(), trx)));
        if (gu_unlikely(i.second == false)) gu_throw_fatal;
        trx_ref = i.first->second;
    }

    return (trx_ref);
}


galera::TrxHandle*
galera::Wsdb::get_trx(const TrxHandle::Params& params,
                      const wsrep_uuid_t&  source_id,
                      wsrep_trx_id_t const trx_id,
                      bool const           create)
{
    TrxHandle* retval(find_trx(trx_id));

    if (0 == retval && create) retval = create_trx(params, source_id, trx_id);

    if (retval != 0) retval->ref();

    return retval;
}


galera::Wsdb::Conn*
galera::Wsdb::get_conn(wsrep_conn_id_t const conn_id, bool const create)
{
    gu::Lock lock(conn_mutex_);

    ConnMap::iterator i(conn_map_.find(conn_id));

    if (conn_map_.end() == i)
    {
        if (create == true)
        {
            std::pair<ConnMap::iterator, bool> p
                (conn_map_.insert(std::make_pair(conn_id, Conn(conn_id))));

            if (gu_unlikely(p.second == false)) gu_throw_fatal;

            return &p.first->second;
        }

        return 0;
    }

    return &(i->second);
}


galera::TrxHandle*
galera::Wsdb::get_conn_query(const TrxHandle::Params& params,
                             const wsrep_uuid_t&  source_id,
                             wsrep_trx_id_t const conn_id,
                             bool const           create)
{
    Conn* const conn(get_conn(conn_id, create));

    if (0 == conn) return 0;

    if (conn->get_trx() == 0 && create == true)
    {
        TrxHandle* trx
            (TrxHandle::New(trx_pool_, params, source_id, conn_id, -1));
        conn->assign_trx(trx);
    }

    return conn->get_trx();
}


void galera::Wsdb::discard_trx(wsrep_trx_id_t trx_id)
{
    gu::Lock lock(trx_mutex_);
    if (trx_id != wsrep_trx_id_t(-1))
    {
        TrxMap::iterator i;
        if ((i = trx_map_.find(trx_id)) != trx_map_.end())
        {
            i->second->unref();
            trx_map_.erase(i);
        }
    }
    else
    {
        ConnTrxMap::iterator i;
        pthread_t id = pthread_self();
        if ((i = conn_trx_map_.find(id)) != conn_trx_map_.end())
        {
            i->second->unref();
            conn_trx_map_.erase(i);
        }
    }
}


void galera::Wsdb::discard_conn_query(wsrep_conn_id_t conn_id)
{
    gu::Lock lock(conn_mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        i->second.assign_trx(0);
    }
}

void galera::Wsdb::discard_conn(wsrep_conn_id_t conn_id)
{
    gu::Lock lock(conn_mutex_);
    ConnMap::iterator i;
    if ((i = conn_map_.find(conn_id)) != conn_map_.end())
    {
        conn_map_.erase(i);
    }
}
