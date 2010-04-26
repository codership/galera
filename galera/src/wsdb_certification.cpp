//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsdb_certification.hpp"
#include "wsdb_trx_handle.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include "wsdb_api.h"

#include <map>



using namespace std;
using namespace gu;


void galera::WsdbCertification::assign_initial_position(wsrep_seqno_t seqno)
{
    assert(seqno >= 0);
    wsdb_set_global_trx_committed(seqno);
    wsdb_purge_trxs_upto(seqno);
}

galera::TrxHandlePtr galera::WsdbCertification::create_trx(
    const void* data,
    size_t data_len,
    wsrep_seqno_t seqno_l,
    wsrep_seqno_t seqno_g)
{
    assert(seqno_l >= 0 && seqno_g >= 0);
    TrxHandlePtr ret(new WsdbTrxHandle(-1, -1, false));
    WsdbTrxHandle* trx(static_cast<WsdbTrxHandle*>(ret.get()));
    struct wsdb_write_set* ws(reinterpret_cast<struct wsdb_write_set*>(gu_malloc(sizeof(struct wsdb_write_set))));

    XDR xdrs;
    
    xdrmem_create(&xdrs, (char *)data, data_len, XDR_DECODE);
    if (!xdr_wsdb_write_set(&xdrs, ws)) {
        gu_fatal("GALERA XDR allocation failed, len: %d seqno: (%lld %lld)", 
                 data_len, seqno_g, seqno_l
            );
        abort();
    }
    
    /* key composition is not sent through xdr */
    if (ws->key_composition) {
        gu_warn("GALERA XDR encoding returned key comp, seqno: (%lld %lld)",
                seqno_g, seqno_l
            );
    }

    trx->assign_write_set(ws);
    trx->assign_seqnos(seqno_l, seqno_g);
    
    Lock lock(mutex_);
    if (trx_map_.insert(make_pair(ret->get_global_seqno(), ret)).second == false)
    {
        gu_throw_fatal;
    }
    
    return ret;
}

int galera::WsdbCertification::append_trx(const TrxHandlePtr& trx)
{

    
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);

    switch (trx->get_write_set().get_type())
    {
    case WSDB_WS_TYPE_TRX:
    {
        const struct wsdb_write_set* ws(
            static_cast<const WsdbWriteSet*>(
                &trx->get_write_set())->write_set_);
        assert(ws != 0 && ws->trx_seqno >= 0);
        return wsdb_append_write_set(
            static_cast<const WsdbWriteSet*>(
                &trx->get_write_set())->write_set_);
    }
    case WSDB_WS_TYPE_CONN:
        return WSDB_OK;
    default:
        gu_throw_fatal << "unknown ws type " << trx->get_write_set().get_type();
        throw;
    }
}

int galera::WsdbCertification::test(const TrxHandlePtr& trx, bool bval)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    struct wsdb_write_set* write_set(
        static_cast<const WsdbWriteSet*>(&trx->get_write_set())->write_set_);
    return wsdb_certification_test(write_set, bval);
}


wsrep_seqno_t galera::WsdbCertification::get_safe_to_discard_seqno() const
{
    return wsdb_get_safe_to_discard_seqno();
}

void galera::WsdbCertification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    assert(seqno >= 0);
    Lock lock(mutex_); 
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    trx_map_.erase(trx_map_.begin(), lower_bound);
    wsdb_purge_trxs_upto(seqno);
}

void galera::WsdbCertification::set_trx_committed(const TrxHandlePtr& trx)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    if (trx->is_local() == true)
    {
        Lock lock(mutex_); 
        wsdb_set_local_trx_committed(trx->get_trx_id());
        wsdb_deref_seqno(trx->get_write_set().get_last_seen_trx());
    }
    else
    {
        wsdb_set_global_trx_committed(trx->get_global_seqno());
    }
}

galera::TrxHandlePtr galera::WsdbCertification::get_trx(wsrep_seqno_t seqno)
{
    Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));
    if (i == trx_map_.end())
    {
        return TrxHandlePtr();
    }
    return i->second;
}

