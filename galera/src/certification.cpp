//
// Copyright (C) 2010-2016 Codership Oy <info@codership.com>
//

#include "certification.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>

using namespace galera;

static const bool cert_debug_on(false);
#define cert_debug                              \
    if (cert_debug_on == false) { }             \
    else log_info << "cert debug: "

#define CERT_PARAM_LOG_CONFLICTS galera::Certification::PARAM_LOG_CONFLICTS

static std::string const CERT_PARAM_PREFIX("cert.");

std::string const galera::Certification::PARAM_LOG_CONFLICTS(CERT_PARAM_PREFIX +
                                                             "log_conflicts");

static std::string const CERT_PARAM_MAX_LENGTH   (CERT_PARAM_PREFIX +
                                                  "max_length");
static std::string const CERT_PARAM_LENGTH_CHECK (CERT_PARAM_PREFIX +
                                                  "length_check");

static std::string const CERT_PARAM_LOG_CONFLICTS_DEFAULT("no");

/*** It is EXTREMELY important that these constants are the same on all nodes.
 *** Don't change them ever!!! ***/
static std::string const CERT_PARAM_MAX_LENGTH_DEFAULT("16384");
static std::string const CERT_PARAM_LENGTH_CHECK_DEFAULT("127");

void
galera::Certification::register_params(gu::Config& cnf)
{
    cnf.add(CERT_PARAM_LOG_CONFLICTS, CERT_PARAM_LOG_CONFLICTS_DEFAULT);
    /* The defaults below are deliberately not reflected in conf: people
     * should not know about these dangerous setting unless they read RTFM. */
    cnf.add(CERT_PARAM_MAX_LENGTH);
    cnf.add(CERT_PARAM_LENGTH_CHECK);
}

/* a function to get around unset defaults in ctor initialization list */
static int
max_length(const gu::Config& conf)
{
    if (conf.is_set(CERT_PARAM_MAX_LENGTH))
        return conf.get<int>(CERT_PARAM_MAX_LENGTH);
    else
        return gu::Config::from_config<int>(CERT_PARAM_MAX_LENGTH_DEFAULT);
}

/* a function to get around unset defaults in ctor initialization list */
static int
length_check(const gu::Config& conf)
{
    if (conf.is_set(CERT_PARAM_LENGTH_CHECK))
        return conf.get<int>(CERT_PARAM_LENGTH_CHECK);
    else
        return gu::Config::from_config<int>(CERT_PARAM_LENGTH_CHECK_DEFAULT);
}

// Purge key set from given index
static void purge_key_set(galera::Certification::CertIndexNG& cert_index,
                          galera::TrxHandle*                  trx,
                          const galera::KeySetIn&             key_set,
                          const long                          count)
{
    for (long i(0); i < count; ++i)
    {
        galera::KeyEntryNG ke(key_set.next());
        galera::Certification::CertIndexNG::iterator ci(cert_index.find(&ke));
        assert(ci != cert_index.end());
        if (ci == cert_index.end())
        {
            log_warn << "Could not find key from index";
            continue;
        }
        galera::KeyEntryNG* const kep(*ci);
        KeySet::Key::Prefix const p(ke.key().prefix());
        assert(kep->referenced() == true);

        if (kep->ref_trx(p) == trx)
        {
            kep->unref(p, trx);
            if (kep->referenced() == false)
            {
                cert_index.erase(ci);
                delete kep;
            }
        }
    }
}

void
galera::Certification::purge_for_trx(TrxHandle* trx)
{
    assert(mutex_.owned());
    assert(trx->version() == 3);
    assert(deps_set_.find(trx->global_seqno()) == deps_set_.end());
    const KeySetIn& keys(trx->write_set_in().keyset());
    keys.rewind();
    purge_key_set(cert_index_ng_, trx, keys, keys.count());
}


/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v1to2(const galera::KeyEntryOS* const match,
                         galera::TrxHandle*        const trx,
                         bool                      const full_key,
                         bool                      const exclusive_key,
                         bool                      const log_conflict)
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


/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v3(const galera::KeyEntryNG*   const found,
                      const galera::KeySet::KeyPart&    key,
                      galera::TrxHandle*          const trx,
                      bool                        const log_conflict)
{
    const galera::TrxHandle* const ref_trx(
        found->ref_trx(galera::KeySet::Key::P_EXCLUSIVE));

    if (cert_debug_on && ref_trx)
    {
        cert_debug << "exclusive match: "
                   << *trx << " <-----> " << *ref_trx;
    }

    wsrep_seqno_t const ref_seqno(ref_trx ? ref_trx->global_seqno() : -1);

    // trx should not have any references in index at this point
    assert(ref_trx != trx);

    if (gu_likely(0 != ref_trx))
    {
        // cert conflict takes place if
        // 1) write sets originated from different nodes, are within cert range
        // 2) ref_trx is in isolation mode, write sets are within cert range
        if ((trx->source_id() != ref_trx->source_id() || ref_trx->is_toi()) &&
            ref_seqno >  trx->last_seen_seqno())
        {
            if (gu_unlikely(log_conflict == true))
            {
                log_info << "trx conflict for key " << key << ": "
                         << *trx << " <--X--> " << *ref_trx;
            }
            return true;
        }
    }

    wsrep_seqno_t depends_seqno(ref_seqno);
    galera::KeySet::Key::Prefix const pfx (key.prefix());

    if (pfx == galera::KeySet::Key::P_EXCLUSIVE)
        // exclusive keys must depend on shared refs as well
    {
        const galera::TrxHandle* const ref_shared_trx(
            found->ref_trx(galera::KeySet::Key::P_SHARED));

        assert(ref_shared_trx != trx);

        if (ref_shared_trx)
        {
            cert_debug << "shared match: "
                       << *trx << " <-----> " << *ref_shared_trx;

            depends_seqno = std::max(ref_shared_trx->global_seqno(),
                                     depends_seqno);
        }
    }

    trx->set_depends_seqno(std::max(trx->depends_seqno(), depends_seqno));

    return false;
}


/* returns true on collision, false otherwise */
static bool
certify_v3(galera::Certification::CertIndexNG& cert_index_ng,
           const galera::KeySet::KeyPart&      key,
           galera::TrxHandle*                  trx,
           bool const store_keys, bool const   log_conflicts)
{
    galera::KeyEntryNG ke(key);
    galera::Certification::CertIndexNG::iterator ci(cert_index_ng.find(&ke));

    if (cert_index_ng.end() == ci)
    {
        if (store_keys)
        {
            galera::KeyEntryNG* const kep(new galera::KeyEntryNG(ke));
            ci = cert_index_ng.insert(kep).first;

            cert_debug << "created new entry";
        }
        return false;
    }
    else
    {
        cert_debug << "found existing entry";

        galera::KeyEntryNG* const kep(*ci);
        // Note: For we skip certification for isolated trxs, only
        // cert index and key_list is populated.
        return (!trx->is_toi() &&
                certify_and_depend_v3(kep, key, trx, log_conflicts));
    }
}

galera::Certification::TestResult
galera::Certification::do_test_v3(TrxHandle* trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION v3: (" << trx << ") " << *trx;

#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index
    // to original size
    size_t prev_cert_index_size(cert_index_ng_.size());
#endif // NDEBUG

    const KeySetIn& key_set(trx->write_set_in().keyset());
    long const      key_count(key_set.count());
    long            processed(0);


    key_set.rewind();

    for (; processed < key_count; ++processed)
    {
        const KeySet::KeyPart& key(key_set.next());

        if (certify_v3(cert_index_ng_, key, trx, store_keys, log_conflicts_))
        {
            goto cert_fail;
        }
    }

    trx->set_depends_seqno(std::max(trx->depends_seqno(), last_pa_unsafe_));

    if (store_keys == true)
    {
        assert (key_count == processed);

        key_set.rewind();
        for (long i(0); i < key_count; ++i)
        {
            const KeySet::KeyPart& k(key_set.next());
            KeyEntryNG ke(k);
            CertIndexNG::const_iterator ci(cert_index_ng_.find(&ke));

            if (ci == cert_index_ng_.end())
            {
                gu_throw_fatal << "could not find key '" << k
                               << "' from cert index";
            }

            KeyEntryNG* const kep(*ci);

            kep->ref(k.prefix(), k, trx);

        }

        if (trx->pa_unsafe()) last_pa_unsafe_ = trx->global_seqno();

        key_count_ += key_count;
    }
    cert_debug << "END CERTIFICATION (success): " << *trx;
    return TEST_OK;

cert_fail:

    cert_debug << "END CERTIFICATION (failed): " << *trx;

    assert (processed < key_count);

    if (store_keys == true)
    {
        /* Clean up key entries allocated for this trx */
        key_set.rewind();

        /* 'strictly less' comparison is essential in the following loop:
         * processed key failed cert and was not added to index */
        for (long i(0); i < processed; ++i)
        {
            KeyEntryNG ke(key_set.next());

            // Clean up cert index from entries which were added by this trx
            CertIndexNG::iterator ci(cert_index_ng_.find(&ke));

            if (gu_likely(ci != cert_index_ng_.end()))
            {
                KeyEntryNG* kep(*ci);

                if (kep->referenced() == false)
                {
                    // kel was added to cert index by this trx -
                    // remove from cert index and fall through to delete
                    cert_index_ng_.erase(ci);
                }
                else continue;

                assert(kep->referenced() == false);

                delete kep;

            }
            else if(ke.key().shared())
            {
                assert(0); // we actually should never be here, the key should
                           // be either added to cert index or be there already
                log_warn  << "could not find shared key '"
                          << ke.key() << "' from cert index";
            }
            else { /* exclusive can duplicate shared */ }
        }
        assert(cert_index_ng_.size() == prev_cert_index_size);
    }

    return TEST_FAILED;
}

galera::Certification::TestResult
galera::Certification::do_test(const TrxHandlePtr& trx, bool store_keys)
{
    assert(trx->source_id() != WSREP_UUID_UNDEFINED);

    if (trx->version() != version_)
    {
        log_warn << "trx protocol version: "
                 << trx->version()
                 << " does not match certification protocol version: "
                 << version_;
        return TEST_FAILED;
    }

    if (gu_unlikely(trx->last_seen_seqno() < initial_position_ ||
                    trx->global_seqno() - trx->last_seen_seqno() > max_length_))
    {
        if (trx->global_seqno() - trx->last_seen_seqno() > max_length_)
        {
            log_warn << "certification interval for trx " << *trx
                     << " exceeds the limit of " << max_length_;
        }

        return TEST_FAILED;
    }

    TestResult res(TEST_FAILED);

    gu::Lock lock(mutex_); // why do we need that? - e.g. set_trx_committed()

    /* initialize parent seqno */
    if ((trx->flags() & (TrxHandle::F_ISOLATION | TrxHandle::F_PA_UNSAFE))
        || trx_map_.empty())
    {
        trx->set_depends_seqno(trx->global_seqno() - 1);
    }
    else
    {
        wsrep_seqno_t const ds
            (std::max(trx->depends_seqno(), trx_map_.begin()->first - 1));
        trx->set_depends_seqno(ds);
    }

    switch (version_)
    {
    case 1:
    case 2:
        break;
    case 3:
        res = do_test_v3(trx.get(), store_keys);
        break;
    default:
        gu_throw_fatal << "certification test for version "
                       << version_ << " not implemented";
    }

    assert(TEST_FAILED == res || trx->depends_seqno() >= 0);

    if (store_keys == true && res == TEST_OK)
    {
        ++trx_count_;
        gu::Lock lock(stats_mutex_);
        ++n_certified_;
        deps_dist_ += (trx->global_seqno() - trx->depends_seqno());
        cert_interval_ += (trx->global_seqno() - trx->last_seen_seqno() - 1);
        index_size_ = cert_index_ng_.size();
    }

    byte_count_ += trx->size();

    return res;
}


galera::Certification::TestResult
galera::Certification::do_test_preordered(TrxHandle* trx)
{
    /* Source ID is not always available for preordered events (e.g. event
     * producer didn't provide any) so for now we must accept undefined IDs. */
    //assert(trx->source_id() != WSREP_UUID_UNDEFINED);

    assert(trx->preordered());

    /* we don't want to go any further unless the writeset checksum is ok */
    trx->verify_checksum(); // throws
    /* if checksum failed we need to throw ASAP, let the caller catch it,
     * flush monitors, save state and abort. */

    /* This is a primitive certification test for preordered actions:
     * it does not handle gaps and relies on general apply monitor for
     * parallel applying. Ideally there should be a certification object
     * per source. */

    if (gu_unlikely(last_preordered_id_ &&
                    (last_preordered_id_ + 1 != trx->trx_id())))
    {
        log_warn << "Gap in preordered stream: source_id '" << trx->source_id()
                 << "', trx_id " << trx->trx_id() << ", previous id "
                 << last_preordered_id_;
        assert(0);
    }

    trx->set_depends_seqno(last_preordered_seqno_ -
                           trx->write_set_in().pa_range() + 1);
    // +1 compensates for subtracting from a previous seqno, rather than own.

    last_preordered_seqno_ = trx->global_seqno();
    last_preordered_id_    = trx->trx_id();

    return TEST_OK;
}


galera::Certification::Certification(gu::Config& conf, ServiceThd& thd)
    :
    version_               (-1),
    trx_map_               (),
    cert_index_ng_         (),
    deps_set_              (),
    service_thd_           (thd),
    mutex_                 (),
    trx_size_warn_count_   (0),
    initial_position_      (-1),
    position_              (-1),
    safe_to_discard_seqno_ (-1),
    last_pa_unsafe_        (-1),
    last_preordered_seqno_ (position_),
    last_preordered_id_    (0),
    stats_mutex_           (),
    n_certified_           (0),
    deps_dist_             (0),
    cert_interval_         (0),
    index_size_            (0),
    key_count_             (0),
    byte_count_            (0),
    trx_count_             (0),

    max_length_            (max_length(conf)),
    max_length_check_      (length_check(conf)),
    log_conflicts_         (conf.get<bool>(CERT_PARAM_LOG_CONFLICTS))
{}


galera::Certification::~Certification()
{
    log_info << "cert index usage at exit "   << cert_index_ng_.size();
    log_info << "cert trx map usage at exit " << trx_map_.size();
    log_info << "deps set usage at exit "     << deps_set_.size();

    double avg_cert_interval(0);
    double avg_deps_dist(0);
    size_t index_size(0);
    stats_get(avg_cert_interval, avg_deps_dist, index_size);
    log_info << "avg deps dist "              << avg_deps_dist;
    log_info << "avg cert interval "          << avg_cert_interval;
    log_info << "cert index size "            << index_size;

    gu::Lock lock(mutex_);

    for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
    trx_map_.clear();
    service_thd_.release_seqno(position_);
    service_thd_.flush(gu::UUID());
}


void galera::Certification::assign_initial_position(const gu::GTID& gtid,
                                                    int const       version)
{
    switch (version)
    {
        // value -1 used in initialization when trx protocol version is not
        // available
    case -1:
    case 1:
    case 2:
    case 3:
        break;
    default:
        gu_throw_fatal << "certification/trx version "
                       << version << " not supported";
    }

    wsrep_seqno_t const seqno(gtid.seqno());

    gu::Lock lock(mutex_);

    std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));

    if (seqno >= position_)
    {
        assert(cert_index_ng_.size() == 0);
    }
    else
    {
        if (seqno != -1) // don't warn on index reset.
        {
            log_warn << "moving position backwards: " << position_ << " -> "
                     << seqno;
        }

        std::for_each(cert_index_ng_.begin(), cert_index_ng_.end(),
                      gu::DeleteObject());
        cert_index_ng_.clear();
    }

    trx_map_.clear();
    assert(cert_index_ng_.empty());

    service_thd_.release_seqno(position_);
    service_thd_.flush(gtid.uuid());

    log_info << "Assign initial position for certification: " << gtid
             << ", protocol version: " << version;

    initial_position_      = seqno;
    position_              = seqno;
    safe_to_discard_seqno_ = seqno;
    last_pa_unsafe_        = seqno;
    last_preordered_seqno_ = position_;
    last_preordered_id_    = 0;
    version_               = version;
}


galera::Certification::TestResult
galera::Certification::test(const TrxHandlePtr& trx, bool bval)
{
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0);

    const TestResult ret
        (trx->preordered() ?
         do_test_preordered(trx.get()) : do_test(trx, bval));

    assert(TEST_FAILED == ret || trx->depends_seqno() >= 0);

    if (gu_unlikely(ret != TEST_OK)) { trx->mark_dummy(); }

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


wsrep_seqno_t
galera::Certification::purge_trxs_upto_(wsrep_seqno_t const seqno,
                                        bool const          handle_gcache)
{
    assert (seqno > 0);

    TrxMap::iterator purge_bound(trx_map_.upper_bound(seqno));

    cert_debug << "purging index up to " << seqno;

    for_each(trx_map_.begin(), purge_bound, PurgeAndDiscard(*this));
    trx_map_.erase(trx_map_.begin(), purge_bound);

    if (handle_gcache) service_thd_.release_seqno(seqno);

    if (0 == ((trx_map_.size() + 1) % 10000))
    {
        log_debug << "trx map after purge: length: " << trx_map_.size()
                  << ", requested purge seqno: " << seqno
                  << ", real purge seqno: " << trx_map_.begin()->first - 1;
    }

    return seqno;
}

static inline bool
deps_set_condition(TrxHandle& trx)
{
    // certified trx with local seqno WSREP_SEQNO_UNDEFINED originates from
    // IST so deps set tracking should not be done
    return (trx.is_certified()== true &&
            trx.local_seqno() != WSREP_SEQNO_UNDEFINED &&
            trx.is_dummy()    == false);
    // explicit rollbacks don't pass cert.
}

galera::Certification::TestResult
galera::Certification::append_trx(const TrxHandlePtr& trx)
{
// explicit ROLLBACK is dummy()    assert(!trx->is_dummy());
    assert(trx->global_seqno() >= 0 /* && trx->local_seqno() >= 0 */);
    assert(trx->global_seqno() > position_);

    // trx with local seqno WSREP_SEQNO_UNDEFINED originates from
    // IST so deps set tracking should not be done
    assert(trx->local_seqno() != WSREP_SEQNO_UNDEFINED);

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
            cert_debug
                << "WARNING: last_seen_seqno is below certification index: "
                << trx_map_.begin()->first << " > " << trx->last_seen_seqno();
        }

        position_ = trx->global_seqno();

        if (gu_unlikely(!(position_ & max_length_check_) &&
                        (trx_map_.size() > static_cast<size_t>(max_length_))))
        {
            log_debug << "trx map size: " << trx_map_.size()
                      << " - check if status.last_committed is incrementing";

            wsrep_seqno_t       trim_seqno(position_ - max_length_);
            wsrep_seqno_t const stds      (get_safe_to_discard_seqno_());

            if (trim_seqno > stds)
            {
                log_warn << "Attempt to trim certification index at "
                         << trim_seqno << ", above safe-to-discard: " << stds;
                trim_seqno = stds;
            }
            else
            {
                cert_debug << "purging index up to " << trim_seqno;
            }

            purge_trxs_upto_(trim_seqno, true);
        }
    }

    const TestResult retval(test(trx));

    trx->mark_certified();

    {
        assert(trx->global_seqno() > 0);

        gu::Lock lock(mutex_);
        if (trx_map_.insert(
                std::make_pair(trx->global_seqno(), trx)).second == false)
            gu_throw_fatal << "duplicate trx entry " << *trx;
        if (deps_set_condition(*trx)) deps_set_.insert(trx->last_seen_seqno());
        assert(deps_set_.size() <= trx_map_.size());
    }

    return retval;
}


wsrep_seqno_t galera::Certification::set_trx_committed(TrxHandle& trx)
{
    assert(trx.global_seqno() >= 0 /*&& trx.local_seqno() >= 0*/);
    assert(trx.is_committed() == false);

    wsrep_seqno_t ret(WSREP_SEQNO_UNDEFINED);
    {
        gu::Lock lock(mutex_);

        if (deps_set_condition(trx))
        {
            // trxs with depends_seqno == -1 haven't gone through append_trx
            assert(trx.last_seen_seqno() != WSREP_SEQNO_UNDEFINED);
            DepsSet::iterator i(deps_set_.find(trx.last_seen_seqno()));
            assert(i != deps_set_.end());

            if (deps_set_.size() == 1) safe_to_discard_seqno_ = *i;

            deps_set_.erase(i);
        }

        if (gu_unlikely(index_purge_required()))
        {
            ret = get_safe_to_discard_seqno_();
        }
    }

    trx.mark_committed();

    return ret;
}

void
galera::Certification::set_log_conflicts(const std::string& str)
{
    try
    {
        bool const old(log_conflicts_);
        log_conflicts_ = gu::Config::from_config<bool>(str);
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
                               << CERT_PARAM_LOG_CONFLICTS << '\'';
    }
}

