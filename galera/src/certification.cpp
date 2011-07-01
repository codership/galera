//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#include "uuid.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>

static const bool cert_debug_on(false);
#define cert_debug                              \
    if (cert_debug_on == false) { }             \
    else log_info << "cert debug: "


namespace
{
    class RefTrxListCmpTrx
    {
    public:
        RefTrxListCmpTrx(const galera::TrxHandle* trx) : trx_(trx) { }
        bool operator()(const std::pair<galera::TrxHandle*, bool>& val)
        {
            return (val.first == trx_);
        }
    private:
        const galera::TrxHandle* trx_;
    };

    typedef std::list<std::pair<
                          galera::Certification::CertIndex::iterator, bool> > KeyList;
}


inline galera::KeyEntry::KeyEntry(
    const Key& key)
    :
    key_buf_(0),
    ref_trx_(0),
    ref_full_trx_(0)
{
    const size_t ss(serial_size(key));
    key_buf_ = new gu::byte_t[sizeof(uint32_t) + ss];
    *reinterpret_cast<uint32_t*>(key_buf_) = ss;
    (void)serialize(key, key_buf_ + sizeof(uint32_t), ss, 0);
}

inline galera::KeyEntry::~KeyEntry()
{
    assert(ref_trx_ == 0);
    assert(ref_full_trx_ == 0);
    delete[] key_buf_;
}

inline galera::Key
galera::KeyEntry::get_key() const
{
    Key rk;
    uint32_t ss(*reinterpret_cast<uint32_t*>(key_buf_));
    (void)unserialize(key_buf_ + sizeof(uint32_t), ss, 0, rk);
    return rk;
}


inline const galera::TrxHandle*
galera::KeyEntry::ref_trx() const
{
    return ref_trx_;
}

inline const galera::TrxHandle*
galera::KeyEntry::ref_full_trx() const
{
    return ref_full_trx_;
}


inline void
galera::KeyEntry::ref(TrxHandle* trx, bool full_key)
{
    assert(ref_trx_ == 0 ||
           ref_trx_->global_seqno() <= trx->global_seqno());
    ref_trx_ = trx;
    if (full_key == true)
    {
        ref_full_trx_ = trx;
    }
}


inline void
galera::KeyEntry::unref(TrxHandle* trx, bool full_key)
{
    assert(ref_trx_ != 0);
    if (ref_trx_ == trx) ref_trx_ = 0;
    if (full_key == true && ref_full_trx_ == trx)
    {
        ref_full_trx_ = 0;
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
        KeyEntry* ke(i->first);
        ke->unref(trx, i->second);
        if (ke->ref_trx() == 0)
        {
            assert(ke->ref_full_trx() == 0);
            CertIndex::iterator ci(cert_index_.find(ke->get_key()));
            assert(ci != cert_index_.end());
            delete ci->second;
            cert_index_.erase(ci);
        }
    }
    refs.clear();
}


galera::Certification::TestResult
galera::Certification::do_test_v0(TrxHandle* trx, bool store_keys)
{
    cert_debug << *trx;
    if (trx->flags() & TrxHandle::F_ISOLATION)
    {
        log_debug << "trx in isolation mode: " << *trx;
    }

    const wsrep_seqno_t trx_last_seen_seqno (trx->last_seen_seqno());
    const wsrep_seqno_t trx_global_seqno    (trx->global_seqno());


    size_t offset(serial_size(*trx));
    const MappedBuffer& wscoll(trx->write_set_collection());

    // max_depends_seqno:
    // 1) initialize to the position just before known history
    // 2) maximize on all dependencies
    wsrep_seqno_t max_depends_seqno(trx_map_.empty() ?
                                    position_ - 1    :
                                    trx_map_.begin()->second->global_seqno()
                                    - 1);
    TrxHandle::CertKeySet& match(trx->cert_keys_);
    assert(match.empty() == true);

    gu::Lock lock(mutex_);

    while (offset < wscoll.size())
    {
        WriteSet ws;
        if ((offset = unserialize(&wscoll[0], wscoll.size(), offset, ws)) == 0)
        {
            gu_throw_fatal << "failed to unserialize write set";
        }

        WriteSet::KeySequence rk;
        ws.get_keys(rk);

        // Scan over all row keys
        for (WriteSet::KeySequence::const_iterator i(rk.begin());
             i != rk.end(); ++i)
        {
            typedef std::deque<KeyPart> KPS;
            KPS key_parts(i->key_parts<KPS>());

            if (key_parts.size() == 0)
            {
                // TO isolation
                max_depends_seqno = trx->global_seqno() - 1;
            }

            // Scan over partial matches and full match
            KPS::const_iterator begin(key_parts.begin()), end;
            bool full_key(false);
            for (end = begin; full_key == false;
                 end != key_parts.end() ? ++end : end)
            {
                full_key = (end == key_parts.end());
                CertIndex::iterator ci;
                Key key(begin, end);
                cert_debug << "key: " << key;
                if ((ci = cert_index_.find(key)) != cert_index_.end())
                {
                    // Found matching entry, scan over dependent transactions
                    const TrxHandle* ref_trx(ci->second->ref_full_trx());
                    if (ref_trx == 0)
                    {
                        // trx may have a partial match on own key which does
                        // not have ref_trx assigned yet
                        assert(full_key == false);
                    }
                    else
                    {
                        // We assume that certification is done only once per
                        // trx and in total order
                        const wsrep_seqno_t ref_global_seqno(
                            ref_trx->global_seqno());

                        cert_debug << "trx: " << *trx
                                   << (full_key ? " full " : " partial ")
                                   << "match: " << *ref_trx;
                        assert(ref_global_seqno < trx_global_seqno ||
                               ref_trx->source_id() == trx->source_id());

                        if (((ref_trx->source_id() != trx->source_id()) ||
                             (ref_trx->flags() & TrxHandle::F_ISOLATION)) &&
                            ref_global_seqno     > trx_last_seen_seqno)
                        {
                            // Cert conflict if trx write set didn't
                            // see it committed
                            log_debug << "trx conflict for key "
                                      << key << ": "
                                      << *trx
                                      << " <--X--> "
                                      << *ref_trx;
                            goto cert_fail;
                        }

                        if (ref_global_seqno != trx_global_seqno)
                        {
                            max_depends_seqno = std::max(max_depends_seqno,
                                                         ref_global_seqno);
                        }
                    }
                }
                else if ((full_key == true ||
                          (trx->flags() & TrxHandle::F_ISOLATION)) &&
                         store_keys == true)
                {
                    cert_debug << "store key: " << key;
                    KeyEntry* cie(new KeyEntry(key));
                    ci = cert_index_.insert(std::make_pair(cie->get_key(),
                                                           cie)).first;
                }

                if ((full_key == true ||
                     (trx->flags() & TrxHandle::F_ISOLATION)) &&
                    store_keys == true)
                {
                    cert_debug << "match: " << ci->first;
                    match.push_back(std::make_pair(ci->second, full_key));
                }
            }
        }
    }

    if (store_keys == true)
    {
        trx->set_last_depends_seqno(std::max(safe_to_discard_seqno_,
                                             max_depends_seqno));

        assert(trx->last_depends_seqno() < trx->global_seqno());

        for (TrxHandle::CertKeySet::iterator i(match.begin());
             i != match.end(); ++i)
        {
            i->first->ref(trx, i->second);
        }

    }

    return TEST_OK;

cert_fail:
    purge_for_trx(trx);
    trx->set_last_depends_seqno(-1);
    return TEST_FAILED;
}


static bool
certify_v1(galera::TrxHandle*                              trx,
           galera::Certification::CertIndex&               cert_index,
           galera::WriteSet::KeySequence::const_iterator   key_seq_iter,
           KeyList& key_list, bool store_keys)
{
    typedef std::list<galera::KeyPart> KPS;

    KPS key_parts(key_seq_iter->key_parts<KPS>());
    KPS::const_iterator begin(key_parts.begin()), end;
    bool full_key(false);
    for (end = begin; full_key == false; end != key_parts.end() ? ++end : end)
    {
        full_key = (end == key_parts.end());
        galera::Certification::CertIndex::iterator ci;
        galera::Key key(begin, end);

        cert_debug << "key: " << key
                   << " ("
                   << (full_key == true ? "full" : "partial")
                   << ")";
        if ((ci = cert_index.find(key)) == cert_index.end())
        {
            if (store_keys == false)
            {
                continue;
            }
            galera::KeyEntry* ke(new galera::KeyEntry(key));
            ci = cert_index.insert(std::make_pair(key, ke)).first;
            cert_debug << "created new entry";
        }
        else
        {
            cert_debug << "found existing entry";
        }

        // Note: For we skip certification for isolated trxs, only
        // cert index and key_list is populated.
        if ((trx->flags() & galera::TrxHandle::F_ISOLATION) == 0)
        {
            // 1) if the key is full, match for any trx
            // 2) if the key is partial, match for trx with full key
            const galera::TrxHandle* ref_trx(full_key == true      ?
                                             ci->second->ref_trx() :
                                             ci->second->ref_full_trx());
            if (ref_trx != 0)
            {
                cert_debug << "match ("
                           << (full_key == true ? "full" : "partial")
                           << ") " << *trx << " <-----> " << *ref_trx;
                // trx should not have any references in index at this point
                assert(ref_trx != trx);
                // cert conflict takes place if
                // 1) write sets originated from different nodes, are within
                //    cert range
                // 2) ref_trx is in isolation mode, write sets are within
                //    cert range
                if ((trx->source_id()        != ref_trx->source_id() ||
                     (ref_trx->flags() & galera::TrxHandle::F_ISOLATION) != 0) &&
                    ref_trx->global_seqno() >  trx->last_seen_seqno())
                {
                    log_debug << "trx conflict for key " << key << ": "
                              << *trx << " <--X--> " << *ref_trx;
                    return false;
                }
                trx->set_last_depends_seqno(
                    std::max(trx->last_depends_seqno(),
                             ref_trx->global_seqno()));
            }
        }
        key_list.push_back(std::make_pair(ci, full_key));
    }
    return true;
}


galera::Certification::TestResult
galera::Certification::do_test_v1(TrxHandle* trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION: " << *trx;

    if (trx->flags() & TrxHandle::F_ISOLATION)
    {
        trx->set_last_depends_seqno(trx->global_seqno() - 1);
    }
    else
    {
        trx->set_last_depends_seqno(trx_map_.empty() ?
                                    position_ - 1    :
                                    trx_map_.begin()->second->global_seqno() - 1);
    }

    size_t offset(serial_size(*trx));
    const MappedBuffer& wscoll(trx->write_set_collection());
    std::list<std::pair<CertIndex::iterator, bool> > key_list;
    gu::Lock lock(mutex_);

    // Scan over write sets
    while (offset < wscoll.size())
    {
        WriteSet ws;
        if ((offset = unserialize(&wscoll[0], wscoll.size(), offset, ws)) == 0)
        {
            gu_throw_fatal << "failed to unserialize write set";
        }

        WriteSet::KeySequence rk;
        ws.get_keys(rk);

        // Scan over all keys
        for (WriteSet::KeySequence::const_iterator i(rk.begin());
             i != rk.end(); ++i)
        {
            if (certify_v1(trx, cert_index_, i, key_list, store_keys) == false)
            {
                goto cert_fail;
            }
        }
    }

    if (store_keys == true)
    {
        for (KeyList::iterator i(key_list.begin()); i != key_list.end(); ++i)
        {
            const KeyEntry* ke(i->first->second);
            if ((i->second == false && ke->ref_trx() != trx) ||
                (i->second == true  && ke->ref_full_trx() != trx))
            {
                trx->cert_keys_.push_back(std::make_pair(i->first->second, i->second));
                i->first->second->ref(trx, i->second);
            }
        }
    }
    cert_debug << "END CERTIFICATION (success): " << *trx;
    return TEST_OK;
cert_fail:
    cert_debug << "END CERTIFICATION (failed): " << *trx;
    if (store_keys == true)
    {
        // Clean up cert_index_ entries which were added by this trx
        for (KeyList::iterator i(key_list.begin()); i != key_list.end(); ++i)
        {
            KeyEntry* ke(i->first->second);
            if (ke->ref_trx() == 0)
            {
                assert(ke->ref_full_trx() == 0);
                cert_index_.erase(i->first);
            }
        }
    }
    trx->set_last_depends_seqno(-1);
    return TEST_FAILED;

}

galera::Certification::TestResult
galera::Certification::do_test(TrxHandle* trx, bool store_keys)
{
    if (trx->version() != version_)
    {
        log_info << "trx protocol version: "
                 << trx->version()
                 << " does not match certification protocol version: "
                 << version_;
        return TEST_FAILED;
    }
    if (trx->last_seen_seqno() < initial_position_ ||
        trx->global_seqno() - trx->last_seen_seqno() > max_length_)
    {
        if (trx->last_seen_seqno() < initial_position_)
        {
            log_debug << "last seen seqno below limit for trx " << *trx;
        }

        if (trx->global_seqno() - trx->last_seen_seqno() > max_length_)
        {
            log_warn << "certification interval for trx " << *trx
                     << " exceeds the limit of " << max_length_;
        }

        return TEST_FAILED;
    }

    TestResult res(TEST_FAILED);
    switch (version_)
    {
    case 0:
        res = do_test_v0(trx, store_keys);
        break;
    case 1:
        res = do_test_v1(trx, store_keys);
        break;
    default:
        gu_throw_fatal << "certification test for version "
                       << version_ << " not implemented";
        throw;
    }
    if (store_keys == true && res == TEST_OK)
    {
        ++n_certified_;
        deps_dist_ += (trx->global_seqno() - trx->last_depends_seqno());
    }
    return res;
}


galera::Certification::Certification(const gu::Config& conf)
    :
    version_               (-1),
    trx_map_               (),
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

    for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
}


void galera::Certification::assign_initial_position(wsrep_seqno_t seqno, int version)
{
    if (seqno >= position_)
    {
        purge_trxs_upto(seqno);
        if (cert_index_.size() > 0)
        {
            log_warn << "cert index size afer purge: " << cert_index_.size();
        }
        if (trx_map_.size() > 0)
        {
            log_warn << "trx map size after purge: " << trx_map_.size();
        }
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

    log_info << "Assign initial position for certification: " << seqno
             << ", protocol version: " << version;
    gu::Lock lock(mutex_);
    initial_position_      = seqno;
    position_              = seqno;
    safe_to_discard_seqno_ = seqno;
    version_               = version;
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
        gu::Lock lock(mutex_);

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

    gu::Lock lock(mutex_);

    if (trx_map_.insert(
            std::make_pair(trx->global_seqno(), trx)).second == false)
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
        gu::Lock lock(mutex_);

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
    gu::Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));

    if (i == trx_map_.end()) return 0;

    i->second->ref();

    return i->second;
}
