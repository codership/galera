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
    wsdb_set_global_trx_committed(seqno);
    wsdb_purge_trxs_upto(seqno);
}

galera::TrxHandlePtr galera::WsdbCertification::create_trx(
    const void* data,
    size_t data_len,
    wsrep_seqno_t seqno_l,
    wsrep_seqno_t seqno_g)
{
    TrxHandlePtr ret(new WsdbTrxHandle(-1, -1, false));
    TrxHandlePtr lock(ret);
    WsdbTrxHandle* trx(static_cast<WsdbTrxHandle*>(ret.get()));
    struct wsdb_write_set* ws(reinterpret_cast<struct wsdb_write_set*>(gu_malloc(sizeof(struct wsdb_write_set))));
    
    trx->assign_write_set(ws);
    trx->assign_seqnos(seqno_l, seqno_g);
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
    
    return ret;
}

int galera::WsdbCertification::append_trx(TrxHandlePtr trx)
{
    Lock lock(mutex_);
    
    if (trx_map_.insert(make_pair(trx->get_global_seqno(), trx)).second == false)
    {
        gu_throw_fatal;
    }
    switch (trx->get_write_set().get_type())
    {
    case WSDB_WS_TYPE_TRX:
        assert(static_cast<const WsdbWriteSet*>(
                   &trx->get_write_set())->write_set_ != 0);
        return wsdb_append_write_set(
            static_cast<const WsdbWriteSet*>(
                &trx->get_write_set())->write_set_);
    case WSDB_WS_TYPE_CONN:
        return WSDB_OK;
    default:
        gu_throw_fatal << "unknown ws type " << trx->get_write_set().get_type();
        throw;
    }
}

int galera::WsdbCertification::test(const TrxHandlePtr& trx, bool bval)
{
    struct wsdb_write_set* write_set(0);
    return wsdb_certification_test(write_set, bval);
}


wsrep_seqno_t galera::WsdbCertification::get_safe_to_discard_seqno() const
{
    return wsdb_get_safe_to_discard_seqno();
}

void galera::WsdbCertification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    Lock lock(mutex_); 
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    trx_map_.erase(trx_map_.begin(), lower_bound);
    wsdb_purge_trxs_upto(seqno);
}

void galera::WsdbCertification::set_trx_committed(const TrxHandlePtr& trx)
{
    if (trx->is_local() == true)
    {
        wsdb_set_local_trx_committed(trx->get_trx_id());
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

void galera::WsdbCertification::deref_seqno(wsrep_seqno_t seqno)
{
    wsdb_deref_seqno(seqno);
}


