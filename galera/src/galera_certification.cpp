//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "galera_certification.hpp"
#include "trx_handle.hpp"
#include "galera_write_set.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include "wsdb_api.h"

#include <map>



using namespace std;
using namespace gu;


galera::GaleraCertification::GaleraCertification(const string& conf) 
    : 
    trx_map_(), 
    mutex_(), 
    trx_size_warn_count_(0), 
    position_(-1),
    last_committed_(-1), 
    role_(R_BYPASS)
{ 
    // TODO: Adjust role by configuration
}

galera::GaleraCertification::~GaleraCertification()
{
    log_info << "cert trx map usage at exit " << trx_map_.size();
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
}


void galera::GaleraCertification::assign_initial_position(wsrep_seqno_t seqno)
{
    assert(seqno >= 0);
    position_ = seqno;
}

galera::TrxHandle* galera::GaleraCertification::create_trx(
    const void* data,
    size_t data_len,
    wsrep_seqno_t seqno_l,
    wsrep_seqno_t seqno_g)
{
    assert(seqno_l >= 0 && seqno_g >= 0);
    TrxHandle* trx(new TrxHandle(-1, -1, false));
    
    GaleraWriteSet* ws(new GaleraWriteSet());
    if (unserialize(reinterpret_cast<const byte_t*>(data), 
                    data_len, 0, *ws) == 0)
    {
        gu_throw_fatal << "could not unserialize write set";
    }
    trx->assign_write_set(ws);
    trx->assign_seqnos(seqno_l, seqno_g);
    
    return trx;
}


int galera::GaleraCertification::append_trx(TrxHandle* trx)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    
    if (trx->is_local() == true)
    {
        trx->ref();
    }
    
    {
        Lock lock(mutex_);

        if (trx->get_global_seqno() != position_ + 1)
        {
            log_warn << "seqno gap, position: " << position_
                     << " trx seqno " << trx->get_global_seqno();
        }
        position_ = trx->get_global_seqno();
        if (trx_map_.insert(make_pair(trx->get_global_seqno(), 
                                      trx)).second == false)
        {
            gu_throw_fatal;
        }
        
        if (trx_map_.size() > 10000 && (trx_size_warn_count_++ % 1000 == 0))
        {
            log_warn << "trx map size " << trx_map_.size();
        }
    }
    
    return test(trx);
}


int galera::GaleraCertification::test(const TrxHandle* trx, bool bval)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    switch (role_)
    {
    case R_BYPASS:
        return WSDB_OK;
    default:
        gu_throw_fatal << "role " << role_ << " not implemented";
        throw;
    }
}


wsrep_seqno_t galera::GaleraCertification::get_safe_to_discard_seqno() const
{
    return last_committed_;
}

void galera::GaleraCertification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    assert(seqno >= 0);
    Lock lock(mutex_); 
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    for_each(trx_map_.begin(), lower_bound, Unref2nd<TrxMap::value_type>());
    trx_map_.erase(trx_map_.begin(), lower_bound);
    if (trx_map_.size() > 10000)
    {
        log_warn << "trx map after purge: " 
                 << trx_map_.size() << " " 
                 << trx_map_.begin()->second->get_global_seqno() 
                 << " purge seqno " << seqno;
        log_warn << "last committed seqno updating is probably broken";
    }
}

void galera::GaleraCertification::set_trx_committed(TrxHandle* trx)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    if (last_committed_ < trx->get_global_seqno())
        last_committed_ = trx->get_global_seqno();
}

galera::TrxHandle* galera::GaleraCertification::get_trx(wsrep_seqno_t seqno)
{
    Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));
    if (i == trx_map_.end())
    {
        return 0;
    }
    return i->second;
}

