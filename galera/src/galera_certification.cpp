//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "galera_certification.hpp"
#include "trx_handle.hpp"
#include "write_set.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include "wsdb_api.h"

#include <map>



using namespace std;
using namespace gu;


inline galera::RowKeyEntry::RowKeyEntry(
    const RowKey& row_key)
    :
    row_key_(),
    row_key_buf_(),
    refs_()
{
    // @todo optimize this by passing original row key
    // buffer as argument
    row_key_buf_.resize(serial_size(row_key));
    (void)serialize(row_key, &row_key_buf_[0], 
                    row_key_buf_.size(), 0);
    (void)unserialize(&row_key_buf_[0],
                      row_key_buf_.size(), 0, row_key_);
}

inline const galera::RowKey& 
galera::RowKeyEntry::get_row_key() const 
{ 
    return row_key_; 
}


inline const deque<const galera::TrxHandle*>& 
galera::RowKeyEntry::get_refs() const 
{ 
    return refs_; 
}


inline void 
galera::RowKeyEntry::ref(const TrxHandle* trx)
{
    refs_.push_back(trx);
}


inline void 
galera::RowKeyEntry::unref(const TrxHandle* trx)
{
    std::deque<const TrxHandle*>::iterator 
        i(std::find(refs_.begin(), refs_.end(), trx));
    if (i == refs_.end()) gu_throw_fatal;
    refs_.erase(i);
}


bool 
galera::GaleraCertification::same_source(const TrxHandle* a, const TrxHandle* b)
{
    switch (role_)
    {
    case R_SLAVE:
        return (a->is_local() == b->is_local());
        // @todo Assing source uuid for trx
        // case R_MULTIMASTER:
        // return (a->get_source() == b->get_source());
    default:
        gu_throw_fatal << "not implemented";
        throw;
    }
}

void
galera::GaleraCertification::purge_for_trx(TrxHandle* trx)
{
    TrxHandleLock lock(*trx);
    deque<RowKeyEntry*>& refs(trx->cert_keys_);
    for (deque<RowKeyEntry*>::iterator i = refs.begin(); i != refs.end();
         ++i)
    {
        // Unref all referenced and remove if referenced only by us
        (*i)->unref(trx);
        if ((*i)->get_refs().empty() == true)
        {
            CertIndex::iterator ci(cert_index_.find((*i)->get_row_key()));
            assert(ci != cert_index_.end());
            delete ci->second;
            cert_index_.erase(ci);
        }
    }
    refs.clear();
}


int galera::GaleraCertification::do_test(TrxHandle* trx)
{
    RowKeySequence rk;
    trx->get_write_set().get_keys(rk);
    deque<RowKeyEntry*>& match(trx->cert_keys_);
    wsrep_seqno_t last_depends_seqno(-1);
    assert(match.empty() == true);
    
    // Scan over all row keys
    for (RowKeySequence::const_iterator i = rk.begin(); i != rk.end(); ++i)
    {
        CertIndex::iterator ci;
        if ((ci = cert_index_.find(*i)) != cert_index_.end())
        {
            // Found matching entry, scan over dependent transactions
            const deque<const TrxHandle*>& refs(ci->second->get_refs());
            assert(refs.empty() == false);
            for (deque<const TrxHandle*>::const_iterator ti =
                     refs.begin(); ti != refs.end(); ++ti)
            {
                // We assume that certification is done only once per trx
                // and in total order
                assert((*ti)->get_global_seqno() < trx->get_global_seqno() ||
                       same_source(*ti, trx));
                if (same_source(*ti, trx) == false &&
                    (*ti)->get_global_seqno() > 
                    trx->get_write_set().get_last_seen_trx())
                {
                    // Cert conflict if trx write set didn't see ti committed
                    log_info << "trx conflict";
                    goto cert_fail;
                }
            }
            last_depends_seqno = max(last_depends_seqno, 
                                     refs.back()->get_global_seqno());
        }
        else
        {
            RowKeyEntry* cie(new RowKeyEntry(*i));
            ci = cert_index_.insert(make_pair(cie->get_row_key(), cie)).first;
        }
        match.push_back(ci->second);
        ci->second->ref(trx);
    }
    
    trx->assign_last_depends_seqno(last_depends_seqno);

    return WSDB_OK;
    
cert_fail:
    purge_for_trx(trx);
    return WSDB_CERTIFICATION_FAIL;
}



galera::GaleraCertification::GaleraCertification(const string& conf) 
    : 
    trx_map_(), 
    cert_index_(),
    mutex_(), 
    trx_size_warn_count_(0), 
    position_(-1),
    last_committed_(-1)
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
    
    WriteSet* ws(new WriteSet());
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





int galera::GaleraCertification::test(TrxHandle* trx, bool bval)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    // log_info << "test: " << role_ << " " << trx->is_local();
    switch (role_)
    {
    case R_BYPASS:
        return WSDB_OK;
    case R_SLAVE:
        if (trx->is_local() == false)
        {
            return do_test(trx);
        }
        else
        {
            return WSDB_CERTIFICATION_FAIL;
        }
    case R_MASTER:
        return (trx->is_local() == true ? WSDB_OK : WSDB_CERTIFICATION_FAIL);
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
    for_each(trx_map_.begin(), lower_bound, PurgeAndDiscard(this));
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

