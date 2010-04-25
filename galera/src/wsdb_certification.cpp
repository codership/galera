//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "wsdb_certification.hpp"
#include "wsdb_trx_handle.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

extern "C"
{
#include "wsdb_api.h"
}
#include <map>



using namespace std;
using namespace gu;


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
    return wsdb_append_write_set(0);
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
    wsdb_purge_trxs_upto(seqno);
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    trx_map_.erase(trx_map_.begin(), lower_bound);
}

void galera::WsdbCertification::set_trx_committed(TrxHandlePtr trx)
{
    if (trx->is_local() == true)
    {
        wsdb_set_local_trx_committed(trx->get_global_seqno());
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


