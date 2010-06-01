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
    ref_trx_(0)
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


inline galera::TrxHandle*
galera::RowKeyEntry::get_ref_trx() const
{
    return ref_trx_;
}


inline void
galera::RowKeyEntry::ref(TrxHandle* trx)
{
    assert(ref_trx_ == 0 ||
           ref_trx_->get_global_seqno() < trx->get_global_seqno());
    ref_trx_ = trx;
}


inline void
galera::RowKeyEntry::unref(TrxHandle* trx)
{
    if (ref_trx_ == trx)
    {
        ref_trx_ = 0;
    }
}


void
galera::GaleraCertification::purge_for_trx(TrxHandle* trx)
{
    TrxHandle::CertKeySet& refs(trx->cert_keys_);
    for (TrxHandle::CertKeySet::iterator i = refs.begin(); i != refs.end();
         ++i)
    {
        // Unref all referenced and remove if was referenced by us
        (*i)->unref(trx);
        if ((*i)->get_ref_trx() == 0)
        {
            CertIndex::iterator ci(cert_index_.find((*i)->get_row_key()));
            assert(ci != cert_index_.end());
            delete ci->second;
            cert_index_.erase(ci);
        }
    }
    refs.clear();
}

int galera::GaleraCertification::do_test(TrxHandle* trx, bool store_keys)
{
    size_t offset(0);
    const MappedBuffer& wscoll(trx->get_write_set_collection());

    // max_depends_seqno, start from -1 and maximize on all dependencies
    // min_depends_seqno, start from last seen and minimize on all dependencies
    // last depends seqno is max(min_depends_seqno, max_depends_seqno)
    wsrep_seqno_t max_depends_seqno(-1);
    wsrep_seqno_t min_depends_seqno(trx->get_last_seen_seqno());
    const wsrep_seqno_t trx_global_seqno(trx->get_global_seqno());
    TrxHandle::CertKeySet& match(trx->cert_keys_);
    assert(match.empty() == true);

    Lock lock(mutex_);

    while (offset < wscoll.size())
    {
        WriteSet ws;
        if ((offset = unserialize(&wscoll[0], wscoll.size(), offset, ws)) == 0)
        {
            gu_throw_fatal << "failed to unserialize write set";
        }

        RowKeySequence rk;
        ws.get_keys(rk);

        // Scan over all row keys
        for (RowKeySequence::const_iterator i = rk.begin(); i != rk.end(); ++i)
        {
            CertIndex::iterator ci;
            if ((ci = cert_index_.find(*i)) != cert_index_.end())
            {
                // Found matching entry, scan over dependent transactions
                const TrxHandle* ref_trx(ci->second->get_ref_trx());
                assert(ref_trx != 0);
                // We assume that certification is done only once per trx
                // and in total order
                const wsrep_seqno_t ref_global_seqno(ref_trx->get_global_seqno());

                assert(ref_global_seqno < trx_global_seqno ||
                       ref_trx->get_source_id() == trx->get_source_id());
                if (ref_trx->get_source_id() != trx->get_source_id() &&
                    ref_global_seqno > trx->get_last_seen_seqno())
                {
                    // Cert conflict if trx write set didn't see ti committed
                    log_debug << "trx conflict " << ref_global_seqno
                              << " " << trx->get_last_seen_seqno();
                    goto cert_fail;
                }
                if (ref_global_seqno != trx_global_seqno)
                {
                    max_depends_seqno = max(max_depends_seqno,
                                            ref_global_seqno);
                    min_depends_seqno = min(min_depends_seqno,
                                            ref_global_seqno);
                }
            }
            else if (store_keys == true)
            {
                RowKeyEntry* cie(new RowKeyEntry(*i));
                ci = cert_index_.insert(make_pair(cie->get_row_key(), cie)).first;
            }

            if (store_keys == true)
            {
                match.insert(ci->second);

            }
        }
    }

    if (store_keys == true)
    {
        trx->assign_last_depends_seqno(max(min_depends_seqno,
                                           max_depends_seqno));
        assert(trx->get_last_depends_seqno() < trx->get_global_seqno());
        for (TrxHandle::CertKeySet::iterator i = trx->cert_keys_.begin();
             i != trx->cert_keys_.end(); ++i)
        {
            (*i)->ref(trx);
        }

        trx->set_certified();
        ++n_certified_;
        deps_dist_ += (trx->get_global_seqno() - trx->get_last_depends_seqno());
    }

    return WSDB_OK;

cert_fail:
    purge_for_trx(trx);
    trx->assign_last_depends_seqno(-1);
    return WSDB_CERTIFICATION_FAIL;
}



galera::GaleraCertification::GaleraCertification(const string& conf)
    :
    trx_map_(),
    cert_index_(),
    deps_set_(),
    mutex_(),
    trx_size_warn_count_(0),
    position_(-1),
    safe_to_discard_seqno_(-1),
    n_certified_(0),
    deps_dist_(0)
{
    // TODO: Adjust role by configuration
}




galera::GaleraCertification::~GaleraCertification()
{
    log_info << "cert index usage at exit " << cert_index_.size();
    log_info << "cert trx map usage at exit " << trx_map_.size();
    log_info << "deps set usage at exit " << deps_set_.size();
    log_info << "avg deps dist " << get_avg_deps_dist();
    for_each(cert_index_.begin(), cert_index_.end(), DiscardRK());
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
}


void galera::GaleraCertification::assign_initial_position(wsrep_seqno_t seqno)
{
    assert(seqno >= 0 && seqno >= position_);
    {
        Lock lock(mutex_);
        position_ = seqno;
        safe_to_discard_seqno_ = seqno;
    }
    purge_trxs_upto(position_);
}

galera::TrxHandle* galera::GaleraCertification::create_trx(
    const void* data,
    size_t data_len,
    wsrep_seqno_t seqno_l,
    wsrep_seqno_t seqno_g)
{
    assert(seqno_l >= 0 && seqno_g >= 0);

    TrxHandle* trx(0);
    WriteSet ws;
    size_t offset(0);
    while (offset < data_len)
    {
        offset = unserialize(reinterpret_cast<const byte_t*>(data),
                             data_len, offset, ws);
        if (trx == 0)
        {
            trx = new TrxHandle(ws.get_source_id(),
                                ws.get_conn_id(), ws.get_trx_id(), false);
        }
        trx->assign_last_seen_seqno(ws.get_last_seen_trx());
        trx->assign_write_set_type(ws.get_type());
        trx->assign_write_set_flags(WriteSet::F_COMMIT);
    }
    trx->assign_seqnos(seqno_l, seqno_g);
    trx->append_write_set(data, data_len);

    assert(offset == data_len);
    return trx;
}


int galera::GaleraCertification::append_trx(TrxHandle* trx)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);
    assert(trx->get_global_seqno() > position_);

    if (trx->is_local() == true)
    {
        trx->ref();
    }

    {
        Lock lock(mutex_);

        if (trx->get_global_seqno() != position_ + 1)
        {
            // this is perfectly normal if trx is rolled back just after
            // replication, keeping the log though
            log_debug << "seqno gap, position: " << position_
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
            log_warn << "trx map size " << trx_map_.size()
                     << " check if status.last_committed is incrementing";
        }
    }

    const int retval(test(trx));

    if (trx->get_last_depends_seqno() > -1)
    {
        Lock lock(mutex_);
        deps_set_.insert(trx->get_last_seen_seqno());
        assert(deps_set_.size() <= trx_map_.size());
        // assert(trx->get_last_depends_seqno() >= trx->get_last_seen_seqno());
    }
    else
    {
        // we cleanup here so that caller can just forget about the trx
        assert(retval != WSDB_OK);
        trx->set_committed();
        trx->clear();
    }

    return retval;
}


int galera::GaleraCertification::test(TrxHandle* trx, bool bval)
{
    assert(trx->get_global_seqno() >= 0 && trx->get_local_seqno() >= 0);

    if (bval == true)
    {
        // optimistic guess, cert test may adjust this to tighter value
        trx->assign_last_depends_seqno(trx->get_last_seen_seqno());
    }
    switch (role_)
    {
    case R_BYPASS:
        return WSDB_OK;
    case R_MASTER_SLAVE:
    case R_MULTIMASTER:
        return do_test(trx, bval);
    default:
        gu_throw_fatal << "role " << role_ << " not implemented";
        throw;
    }
}


wsrep_seqno_t galera::GaleraCertification::get_safe_to_discard_seqno() const
{
    Lock lock(mutex_);
    wsrep_seqno_t retval;
    if (deps_set_.empty() == true)
    {
        retval = safe_to_discard_seqno_;
    }
    else
    {
        retval = *deps_set_.begin();
    }
    return retval;
}


void galera::GaleraCertification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    assert(seqno >= 0 && seqno <= get_safe_to_discard_seqno());
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

    if (trx->get_last_depends_seqno() > -1)
    {
        // trxs with last_depends_seqno == -1 haven't gone through
        // append_trx
        Lock lock(mutex_);
        DepsSet::iterator i(deps_set_.find(trx->get_last_seen_seqno()));
        assert(i != deps_set_.end());
        if (deps_set_.size() == 1)
        {
            safe_to_discard_seqno_ = *i;
        }
        deps_set_.erase(i);
    }

    trx->set_committed();
    trx->clear();
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
