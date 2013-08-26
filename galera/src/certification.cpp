//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
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

std::string const
galera::Certification::Param::log_conflicts =
                     CERTIFICATION_PARAM_LOG_CONFLICTS_STR;

std::string const
galera::Certification::Defaults::log_conflicts =
                     CERTIFICATION_DEFAULTS_LOG_CONFLICTS_STR;

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
        KeyEntry* const kel(i->first);

        const bool full_key(i->second.first);
        const bool shared(i->second.second);

        CertIndex::iterator ci(cert_index_.find(kel));
        assert(ci != cert_index_.end());
        KeyEntry* const ke(*ci);

        if (shared == false &&
            (ke->ref_trx() == trx || ke->ref_full_trx() == trx))
        {
            ke->unref(trx, full_key);
        }

        if (shared == true &&
            (ke->ref_shared_trx() == trx || ke->ref_full_shared_trx() == trx))
        {
            ke->unref_shared(trx, full_key);
        }

        if (ke->ref_trx() == 0 && ke->ref_shared_trx() == 0)
        {
            assert(ke->ref_full_trx() == 0);
            assert(ke->ref_full_shared_trx() == 0);
            delete ke;
            cert_index_.erase(ci);
        }

        if (kel != ke) delete kel;
    }
}


/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v1to2(const galera::KeyEntry* const match,
                         galera::TrxHandle*      const trx,
                         bool                    const full_key,
                         bool                    const exclusive_key,
                         bool                    const log_conflict)
{
    // 1) if the key is full, match for any trx
    // 2) if the key is partial, match for trx with full key
    const galera::TrxHandle* const ref_trx(full_key == true ?
                                           match->ref_trx() :
                                           match->ref_full_trx());

    if (cert_debug_on && ref_trx)
    {
        cert_debug << "exclusive match ("
                   << (full_key == true ? "full" : "partial")
                   << ") " << *trx << " <-----> " << *ref_trx;
    }

    wsrep_seqno_t const ref_seqno(ref_trx ? ref_trx->global_seqno() : -1);

    // trx should not have any references in index at this point
    assert(ref_trx != trx);

    if (gu_likely(0 != ref_trx))
    {
        // cert conflict takes place if
        // 1) write sets originated from different nodes, are within cert range
        // 2) ref_trx is in isolation mode, write sets are within cert range
        if ((trx->source_id() != ref_trx->source_id() ||
             (ref_trx->flags() & galera::TrxHandle::F_ISOLATION) != 0) &&
            ref_seqno >  trx->last_seen_seqno())
        {
            if (gu_unlikely(log_conflict == true))
            {
                log_info << "trx conflict for key "
                         << match->get_key(ref_trx->version())
                         << ": " << *trx << " <--X--> " << *ref_trx;
            }
            return true;
        }
    }

    wsrep_seqno_t depends_seqno(ref_seqno);

    if (exclusive_key) // exclusive keys must depend on shared refs as well
    {
        const galera::TrxHandle* const ref_shared_trx(full_key == true ?
                                                      match->ref_shared_trx() :
                                                      match->ref_full_shared_trx());
        assert(ref_shared_trx != trx);

        if (ref_shared_trx)
        {
            cert_debug << "shared match ("
                       << (full_key == true ? "full" : "partial")
                       << ") " << *trx << " <-----> " << *ref_shared_trx;

            depends_seqno = std::max(ref_shared_trx->global_seqno(),
                                     depends_seqno);
        }
    }

    trx->set_depends_seqno(std::max(trx->depends_seqno(), depends_seqno));

    return false;
}


static bool
certify_v1to2(galera::TrxHandle*                            trx,
              galera::Certification::CertIndex&             cert_index,
              const galera::Key&                            key,
              galera::TrxHandle::CertKeySet&                key_list,
              bool const store_keys, bool const log_conflicts)
{
    typedef std::list<galera::KeyPart> KPS;

    KPS key_parts(key.key_parts<KPS>());
    KPS::const_iterator begin(key_parts.begin()), end;
    bool full_key(false);
    for (end = begin; full_key == false; end != key_parts.end() ? ++end : end)
    {
        full_key = (end == key_parts.end());
        galera::Certification::CertIndex::iterator ci;
        galera::KeyEntry ke(key.version(), begin, end, key.flags());

        cert_debug << "key: " << ke.get_key()
                   << " (" << (full_key == true ? "full" : "partial") << ")";

        bool const shared_key(ke.get_key().flags() & galera::Key::F_SHARED);

        if (store_keys && (key_list.find(&ke) != key_list.end()))
        {
            // avoid certification for duplicates
            // should be removed once we can eleminate dups on deserialization
            continue;
        }

        galera::KeyEntry* kep;

        if ((ci = cert_index.find(&ke)) == cert_index.end())
        {
            if (store_keys)
            {
                kep = new galera::KeyEntry(ke);
                ci = cert_index.insert(kep).first;
                cert_debug << "created new entry";
            }
        }
        else
        {
            cert_debug << "found existing entry";

            // Note: For we skip certification for isolated trxs, only
            // cert index and key_list is populated.
            if ((trx->flags() & galera::TrxHandle::F_ISOLATION) == 0 &&
                certify_and_depend_v1to2(*ci, trx, full_key,
                                         !shared_key, log_conflicts))
            {
                return false;
            }

            if (store_keys)
            {
                if (gu_likely(
                        true == ke.get_key().equal_all((*ci)->get_key())))
                {
                    kep = *ci;
                }
                else
                {
                    // duplicate with different flags - need to store a copy
                    kep = new galera::KeyEntry(ke);
                }
            }
        }

        if (store_keys)
        {
            key_list.insert(std::make_pair(kep, std::make_pair(full_key,
                                                               shared_key)));
        }

    }

    return true;
}


galera::Certification::TestResult
galera::Certification::do_test_v1to2(TrxHandle* trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION: " << *trx;
    galera::TrxHandle::CertKeySet& key_list(trx->cert_keys_);
    long key_count(0);
    gu::Lock lock(mutex_);

    if ((trx->flags() & (TrxHandle::F_ISOLATION | TrxHandle::F_PA_UNSAFE))
        || trx_map_.empty())
    {
        trx->set_depends_seqno(trx->global_seqno() - 1);
    }
    else
    {
        trx->set_depends_seqno(
            trx_map_.begin()->second->global_seqno() - 1);
    }

#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index_
    // to original size
    size_t prev_cert_index_size(cert_index_.size());
#endif // NDEBUG
    /* Scan over write sets */
    size_t offset(0);
    const gu::byte_t* buf(trx->write_set_buffer().first);
    const size_t buf_len(trx->write_set_buffer().second);
    while (offset < buf_len)
    {
        std::pair<size_t, size_t> k(WriteSet::segment(buf, buf_len, offset));

        // Scan over all keys
        offset = k.first;
        while (offset < k.first + k.second)
        {
            Key key(trx->version());
            offset = unserialize(buf, buf_len, offset, key);
            if (certify_v1to2(trx, cert_index_, key, key_list, store_keys,
                              log_conflicts_) == false)
            {
                goto cert_fail;
            }
            ++key_count;
        }

        // Skip data part
        std::pair<size_t, size_t> d(WriteSet::segment(buf, buf_len, offset));
        offset = d.first + d.second;

    }

    trx->set_depends_seqno(std::max(trx->depends_seqno(), last_pa_unsafe_));

    if (store_keys == true)
    {
        for (TrxHandle::CertKeySet::iterator i(key_list.begin());
             i != key_list.end();)
        {
            KeyEntry* const kel(i->first);
            CertIndex::const_iterator ci(cert_index_.find(kel));

            if (ci == cert_index_.end())
            {
                gu_throw_fatal << "could not find key '"
                               << kel->get_key() << "' from cert index";
            }

            KeyEntry* const ke(*ci);
            const bool full_key(i->second.first);
            const bool shared_key(i->second.second);
            bool keep(false);

            if (shared_key == false)
            {
                if ((full_key == false && ke->ref_trx() != trx) ||
                    (full_key == true  && ke->ref_full_trx() != trx))
                {
                    ke->ref(trx, full_key);
                    keep = true;
                }
            }
            else
            {
                if ((full_key == false && ke->ref_shared_trx() != trx) ||
                    (full_key == true  && ke->ref_full_shared_trx() != trx))
                {
                    ke->ref_shared(trx, full_key);
                    keep = true;
                }
            }

            if (keep)
            {
                ++i;
            }
            else
            {
                // this should not happen with Map, but with List is possible
                i = key_list.erase(i);
                if (kel != ke) delete kel;
            }

        }

        if (trx->pa_unsafe()) last_pa_unsafe_ = trx->global_seqno();

        key_count_ += key_count;
    }
    cert_debug << "END CERTIFICATION (success): " << *trx;
    return TEST_OK;
cert_fail:
    cert_debug << "END CERTIFICATION (failed): " << *trx;
    if (store_keys == true)
    {
        // Clean up key entries allocated for this trx
        for (TrxHandle::CertKeySet::iterator i(key_list.begin());
             i != key_list.end(); ++i)
        {
            KeyEntry* const kel(i->first);

            // Clean up cert_index_ from entries which were added by this trx
            CertIndex::iterator ci(cert_index_.find(kel));

            if (ci != cert_index_.end())
            {
                KeyEntry* ke(*ci);

                if (ke->ref_trx() == 0 && ke->ref_shared_trx() == 0)
                {
                    // kel was added to cert_index_ by this trx -
                    // remove from cert_index_ and fall through to delete
                    if (ke->get_key().flags() != kel->get_key().flags())
                    {
                        // two copies of keys in key list, shared and exclusive,
                        // skip the one which was not used to create key entry
                        assert(key_list.find(ke) != key_list.end());
                        continue;
                    }
                    assert(ke->ref_full_trx() == 0);
                    assert(ke->ref_full_shared_trx() == 0);
                    assert(kel == ke);
                    cert_index_.erase(ci);
                }
                else if (ke == kel)
                {
                    // kel was added and is referenced by another trx - skip it
                    continue;
                }
                // else kel != ke : kel is a duplicate of ke with different
                //                  flags, fall through to delete
            }
            else
            {
                assert(0); // we actually should never be here, the key should
                           // be either added to cert_index_ or be there already
                log_warn  << "could not find key '"
                          << kel->get_key() << "' from cert index";
            }

            assert(kel->ref_trx() == 0);
            assert(kel->ref_shared_trx() == 0);
            assert(kel->ref_full_trx() == 0);
            assert(kel->ref_full_shared_trx() == 0);
            delete kel;
        }
        assert(cert_index_.size() == prev_cert_index_size);
    }

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

    if (gu_unlikely(trx->last_seen_seqno() < initial_position_ ||
                    trx->global_seqno() - trx->last_seen_seqno() > max_length_))
    {
        if (trx->last_seen_seqno() < initial_position_)
        {
            if (cert_index_.empty() == false)
            {
                log_warn << "last seen seqno below limit for trx " << *trx;
            }
            else
            {
                log_debug << "last seen seqno below limit for trx " << *trx;
            }
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
    case 1:
    case 2:
        res = do_test_v1to2(trx, store_keys);
        break;
    default:
        gu_throw_fatal << "certification test for version "
                       << version_ << " not implemented";
    }

    if (store_keys == true && res == TEST_OK)
    {
        ++n_certified_;
        deps_dist_ += (trx->global_seqno() - trx->depends_seqno());
    }

    return res;
}


galera::Certification::Certification(gu::Config& conf)
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
    last_pa_unsafe_        (-1),
    n_certified_           (0),
    deps_dist_             (0),

    /* The defaults below are deliberately not reflected in conf: people
     * should not know about these dangerous setting unless they read RTFM. */
    max_length_           (conf.get<long>("cert.max_length",
                                          max_length_default)),
    max_length_check_     (conf.get<unsigned long>("cert.max_length_check",
                                                   max_length_check_default)),
    log_conflicts_        (false),
    key_count_            (0)
{
    try // this is for unit tests where conf may lack some parameters
    {
        log_conflicts_ = conf.get<bool>(Param::log_conflicts);
    }
    catch (gu::NotFound& e)
    {
        conf.set(Param::log_conflicts, Defaults::log_conflicts);
        log_conflicts_ = conf.get<bool>(Param::log_conflicts);
    }
}


galera::Certification::~Certification()
{
    log_info << "cert index usage at exit "   << cert_index_.size();
    log_info << "cert trx map usage at exit " << trx_map_.size();
    log_info << "deps set usage at exit "     << deps_set_.size();
    log_info << "avg deps dist "              << get_avg_deps_dist();

    for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
}


void galera::Certification::assign_initial_position(wsrep_seqno_t seqno,
                                                    int           version)
{
    switch (version)
    {
        // value -1 used in initialization when trx protocol version is not
        // available
    case -1:
    case 1:
    case 2:
        break;
    default:
        gu_throw_fatal << "certification/trx version "
                       << version << " not supported";
    }

    if (seqno >= position_)
    {
        std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
        assert(cert_index_.size() == 0);
        trx_map_.clear();
    }
    else
    {
        log_warn << "moving position backwards: " << position_ << " -> "
                 << seqno;
        std::for_each(cert_index_.begin(), cert_index_.end(),
                      gu::DeleteObject());
        std::for_each(trx_map_.begin(), trx_map_.end(),
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
    last_pa_unsafe_        = seqno;
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

        if (gu_unlikely((trx->last_seen_seqno() + 1) < trx_map_.begin()->first))
        {
            /* See #733 - for now it is false positive */
            cert_debug << "WARNING: last_seen_seqno is below certification index: "
            << trx_map_.begin()->first << " > " << trx->last_seen_seqno();
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
                cert_debug << "purging index up to " << trim_seqno;
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

    const TestResult ret(do_test(trx, bval));

    if (gu_unlikely(ret != TEST_OK))
    {
        // make sure that last depends seqno is -1 for trxs that failed
        // certification
        trx->set_depends_seqno(WSREP_SEQNO_UNDEFINED);
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
    cert_debug << "purging index up to " << lower_bound->first;
    for_each(trx_map_.begin(), lower_bound, PurgeAndDiscard(*this));
    trx_map_.erase(trx_map_.begin(), lower_bound);
    if (0 == ((trx_map_.size() + 1) % 10000))
    {
        log_debug << "trx map after purge: length: " << trx_map_.size()
                  << ", purge seqno " << seqno;
    }
}


wsrep_seqno_t galera::Certification::set_trx_committed(TrxHandle* trx)
{
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0 &&
           trx->is_committed() == false);

    wsrep_seqno_t ret(-1);
    {
        gu::Lock lock(mutex_);
        if (trx->is_certified() == true)
        {
            // trxs with depends_seqno == -1 haven't gone through
            // append_trx
            DepsSet::iterator i(deps_set_.find(trx->last_seen_seqno()));
            assert(i != deps_set_.end());

            if (deps_set_.size() == 1) safe_to_discard_seqno_ = *i;

            deps_set_.erase(i);
        }

        if (gu_unlikely(index_purge_required()))
        {
            ret = get_safe_to_discard_seqno_();
        }
    }

    trx->mark_committed();
    trx->clear();

    return ret;
}

galera::TrxHandle* galera::Certification::get_trx(wsrep_seqno_t seqno)
{
    gu::Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));

    if (i == trx_map_.end()) return 0;

    i->second->ref();

    return i->second;
}

void
galera::Certification::set_log_conflicts(const std::string& str)
{
    try
    {
        bool const old(log_conflicts_);
        log_conflicts_ = gu::from_string<bool>(str);
        if (old != log_conflicts_)
        {
            log_info << (log_conflicts_ ? "Enabled" : "Disabled")
                     << " logging of certification conflicts.";
        }
    }
    catch (gu::NotFound& e)
    {
        gu_throw_error(EINVAL) << "Bad value '" << str
                               << "' for boolean parameter '"
                               << Param::log_conflicts << '\'';
    }
}

