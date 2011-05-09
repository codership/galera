//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#include "uuid.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>

using namespace std;
using namespace gu;

inline galera::RowIdEntry::RowIdEntry(
    const RowId& row_id)
    :
    row_id_buf_(0),
    ref_trx_(0)
{
    const size_t ss(serial_size(row_id));
    row_id_buf_ = new gu::byte_t[sizeof(uint32_t) + ss];
    *reinterpret_cast<uint32_t*>(row_id_buf_) = ss;
    (void)serialize(row_id, row_id_buf_ + sizeof(uint32_t), ss, 0);
}

inline galera::RowIdEntry::~RowIdEntry()
{
    delete[] row_id_buf_;
}

inline galera::RowId
galera::RowIdEntry::get_row_id() const
{
    RowId rk;
    uint32_t ss(*reinterpret_cast<uint32_t*>(row_id_buf_));
    (void)unserialize(row_id_buf_ + sizeof(uint32_t), ss, 0, rk);
    return rk;
}


inline galera::TrxHandle*
galera::RowIdEntry::get_ref_trx() const
{
    return ref_trx_;
}


inline void
galera::RowIdEntry::ref(TrxHandle* trx)
{
    assert(ref_trx_ == 0 ||
           ref_trx_->global_seqno() < trx->global_seqno());
    ref_trx_ = trx;
}


inline void
galera::RowIdEntry::unref(TrxHandle* trx)
{
    if (ref_trx_ == trx)
    {
        ref_trx_ = 0;
    }
}


/*** It is EXTREMELY important that these constants are the same on all nodes.
 *** Don't change them ever!!! ***/
long const
galera::Certification::max_length_default = 16384;

unsigned long const
galera::Certification::max_length_check_default = 127;


void
galera::Certification::purge_for_trx(TrxHandle* trx)
{
    TrxHandle::CertKeySet& refs(trx->cert_keys_);
    for (TrxHandle::CertKeySet::iterator i = refs.begin(); i != refs.end();
         ++i)
    {
        // Unref all referenced and remove if was referenced by us
        (*i)->unref(trx);
        if ((*i)->get_ref_trx() == 0)
        {
            CertIndex::iterator ci(cert_index_.find((*i)->get_row_id()));
            assert(ci != cert_index_.end());
            delete ci->second;
            cert_index_.erase(ci);
        }
    }
    refs.clear();
}


galera::Certification::TestResult
galera::Certification::do_test(TrxHandle* trx, bool store_keys)
{
    const wsrep_seqno_t trx_last_seen_seqno (trx->last_seen_seqno());
    const wsrep_seqno_t trx_global_seqno    (trx->global_seqno());

    if (gu_unlikely(trx_last_seen_seqno < initial_position_ ||
                    (trx_global_seqno - trx_last_seen_seqno) > max_length_))
    {
        if (trx_last_seen_seqno < initial_position_)
        {
            log_debug << "last seen seqno below limit for trx " << *trx;
        }

        if ((trx_global_seqno - trx_last_seen_seqno) > max_length_)
        {
            log_warn << "certification interval for trx " << *trx
                     << " exceeds the limit of " << max_length_;
        }

        return TEST_FAILED;
    }

    size_t offset(serial_size(*trx));
    const MappedBuffer& wscoll(trx->write_set_collection());

    // max_depends_seqno, start from -1 and maximize on all dependencies
    wsrep_seqno_t max_depends_seqno(to_isolation_.empty() == true ? -1 :
                                    to_isolation_.rbegin()->first);
    TrxHandle::CertKeySet& match(trx->cert_keys_);
    assert(match.empty() == true);

    Lock lock(mutex_);

    // this must be run in TO isolation
    if (trx->trx_id() == static_cast<wsrep_trx_id_t>(-1))
    {
        if (to_isolation_.insert(std::make_pair(trx->global_seqno(),
                                                trx)).second == false)
        {
            gu_throw_fatal << "duplicate trx entry " << *trx;
        }
        max_depends_seqno = trx->global_seqno() - 1;
    }

    while (offset < wscoll.size())
    {
        WriteSet ws;
        if ((offset = unserialize(&wscoll[0], wscoll.size(), offset, ws)) == 0)
        {
            gu_throw_fatal << "failed to unserialize write set";
        }

        RowIdSequence rk;
        ws.get_row_ids(rk);

        // Scan over all row keys
        for (RowIdSequence::const_iterator i = rk.begin(); i != rk.end(); ++i)
        {
            CertIndex::iterator ci;
            if ((ci = cert_index_.find(*i)) != cert_index_.end())
            {
                // Found matching entry, scan over dependent transactions
                const TrxHandle* ref_trx(ci->second->get_ref_trx());
                // We assume that certification is done only once per trx
                // and in total order
                const wsrep_seqno_t ref_global_seqno(ref_trx->global_seqno());

                assert(ref_global_seqno < trx_global_seqno ||
                       ref_trx->source_id() == trx->source_id());

                if (ref_trx->source_id() != trx->source_id() &&
                    ref_global_seqno > trx_last_seen_seqno)
                {
                    // Cert conflict if trx write set didn't see it committed
                    log_debug << "trx conflict " << ref_global_seqno
                              << " " << trx_last_seen_seqno;
                    goto cert_fail;
                }

                if (ref_global_seqno != trx_global_seqno)
                {
                    max_depends_seqno = max(max_depends_seqno,
                                            ref_global_seqno);
                }
            }
            else if (store_keys == true)
            {
                RowIdEntry* cie(new RowIdEntry(*i));
                ci = cert_index_.insert(make_pair(cie->get_row_id(), cie)).first;
            }

            if (store_keys == true)
            {
                match.insert(ci->second);
            }
        }
    }

    if (store_keys == true)
    {
        trx->set_last_depends_seqno(max(safe_to_discard_seqno_,
                                        max_depends_seqno));

        assert(trx->last_depends_seqno() < trx->global_seqno());

        for (TrxHandle::CertKeySet::iterator i = trx->cert_keys_.begin();
             i != trx->cert_keys_.end(); ++i)
        {
            (*i)->ref(trx);
        }

        ++n_certified_;
        deps_dist_ += (trx->global_seqno() - trx->last_depends_seqno());
    }

    return TEST_OK;

cert_fail:
    purge_for_trx(trx);
    trx->set_last_depends_seqno(-1);
    return TEST_FAILED;
}



galera::Certification::Certification(const gu::Config& conf)
    :
    trx_map_               (),
    to_isolation_          (),
    cert_index_            (),
    deps_set_              (),
    mutex_                 (),
    trx_size_warn_count_   (0),
    initial_position_      (-1),
    position_              (-1),
    safe_to_discard_seqno_ (-1),
    n_certified_           (0),
    deps_dist_             (0),

    /* The defaults below are deliberately not reflected in conf: people
     * should not know about these dangerous setting uless they read RTFM. */
    max_length_       (conf.get<long>("cert.max_length",
                                      max_length_default)),
    max_length_check_ (conf.get<unsigned long>("cert.max_length_check",
                                               max_length_check_default))
{ }


galera::Certification::~Certification()
{
    log_info << "cert index usage at exit "   << cert_index_.size();
    log_info << "cert trx map usage at exit " << trx_map_.size();
    log_info << "deps set usage at exit "     << deps_set_.size();
    log_info << "avg deps dist "              << get_avg_deps_dist();

    for_each(cert_index_.begin(), cert_index_.end(), DiscardRK());
    for_each(trx_map_.begin(), trx_map_.end(), Unref2nd<TrxMap::value_type>());
}


void galera::Certification::assign_initial_position(wsrep_seqno_t seqno)
{
    if (seqno >= position_)
    {
        purge_trxs_upto(seqno);
    }
    else
    {
        log_warn << "moving position backwards: " << position_ << " -> "
                 << seqno;
        for_each(cert_index_.begin(), cert_index_.end(), DiscardRK());
        for_each(trx_map_.begin(), trx_map_.end(),
                 Unref2nd<TrxMap::value_type>());
        cert_index_.clear();
        trx_map_.clear();
    }

    Lock lock(mutex_);
    initial_position_      = seqno;
    position_              = seqno;
    safe_to_discard_seqno_ = seqno;
}


galera::Certification::TestResult
galera::Certification::append_trx(TrxHandle* trx)
{
    // todo: enable when source id bug is fixed
    assert(trx->source_id() != WSREP_UUID_UNDEFINED);
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0);
    assert(trx->global_seqno() > position_);

    trx->ref();
    {
        Lock lock(mutex_);

        if (gu_unlikely(trx->global_seqno() != position_ + 1))
        {
            // this is perfectly normal if trx is rolled back just after
            // replication, keeping the log though
            log_debug << "seqno gap, position: " << position_
                      << " trx seqno " << trx->global_seqno();
        }

        position_ = trx->global_seqno();

        if (gu_unlikely(!(position_ & max_length_check_) &&
                        (trx_map_.size() > static_cast<size_t>(max_length_))))
        {
            log_debug << "trx map size: " << trx_map_.size()
                      << " - check if status.last_committed is incrementing";

            const wsrep_seqno_t trim_seqno(position_ - max_length_);
            const wsrep_seqno_t stds(get_safe_to_discard_seqno_());

            if (trim_seqno > stds)
            {
                log_warn << "Attempt to trim certification index at "
                         << trim_seqno << ", above safe-to-discard: " << stds;
                purge_trxs_upto_(stds);
            }
            else
            {
                purge_trxs_upto_(trim_seqno);
            }
        }
    }

    const TestResult retval(test(trx));

    Lock lock(mutex_);

    if (trx_map_.insert(
            make_pair(trx->global_seqno(), trx)).second == false)
        gu_throw_fatal << "duplicate trx entry " << *trx;

    deps_set_.insert(trx->last_seen_seqno());
    assert(deps_set_.size() <= trx_map_.size());
    trx->mark_certified();

    return retval;
}


galera::Certification::TestResult
galera::Certification::test(TrxHandle* trx, bool bval)
{
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0);

    if (bval == true)
    {
        // optimistic guess, cert test may adjust this to tighter value
        trx->set_last_depends_seqno(trx->last_seen_seqno());
    }

    const TestResult ret(do_test(trx, bval));

    if (gu_unlikely(ret != TEST_OK))
    {
        // make sure that last depends seqno is -1 for trxs that failed
        // certification
        trx->set_last_depends_seqno(-1);
    }

    return ret;
}


wsrep_seqno_t galera::Certification::get_safe_to_discard_seqno_() const
{
    wsrep_seqno_t retval;
    if (deps_set_.empty() == true)
    {
        retval = safe_to_discard_seqno_;
    }
    else
    {
        retval = (*deps_set_.begin()) - 1;
    }
    return retval;
}


void galera::Certification::purge_trxs_upto_(wsrep_seqno_t seqno)
{
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    for_each(trx_map_.begin(), lower_bound, PurgeAndDiscard(*this));
    trx_map_.erase(trx_map_.begin(), lower_bound);
    if (0 == ((trx_map_.size() + 1) % 10000))
    {
        log_debug << "trx map after purge: length: " << trx_map_.size()
                  << ", purge seqno " << seqno;
    }
}


void galera::Certification::set_trx_committed(TrxHandle* trx)
{
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0 &&
           trx->is_committed() == false);

    if (trx->is_certified() == true)
    {
        // trxs with last_depends_seqno == -1 haven't gone through
        // append_trx
        Lock lock(mutex_);

        if (trx->trx_id() == static_cast<wsrep_trx_id_t>(-1))
        {
            assert(to_isolation_.find(trx->global_seqno()) !=
                   to_isolation_.end());
            to_isolation_.erase(trx->global_seqno());
        }

        DepsSet::iterator i(deps_set_.find(trx->last_seen_seqno()));
        assert(i != deps_set_.end());

        if (deps_set_.size() == 1) safe_to_discard_seqno_ = *i;

        deps_set_.erase(i);
    }

    trx->mark_committed();
    trx->clear();
}

galera::TrxHandle* galera::Certification::get_trx(wsrep_seqno_t seqno)
{
    Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));

    if (i == trx_map_.end()) return 0;

    i->second->ref();

    return i->second;
}
