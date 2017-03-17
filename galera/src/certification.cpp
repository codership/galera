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
                          galera::TrxHandleSlave*             ts,
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

        if (kep->ref_trx(p) == ts)
        {
            kep->unref(p, ts);
            if (kep->referenced() == false)
            {
                cert_index.erase(ci);
                delete kep;
            }
        }
    }
}

void
galera::Certification::purge_for_trx(TrxHandleSlave* trx)
{
    assert(mutex_.owned());
    assert(trx->version() == 3 || trx->version() == 4);
    const KeySetIn& keys(trx->write_set().keyset());
    keys.rewind();
    purge_key_set(cert_index_ng_, trx, keys, keys.count());
}

/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v3(const galera::KeyEntryNG*   const found,
                      const galera::KeySet::KeyPart&    key,
                      galera::TrxHandleSlave*     const trx,
                      bool                        const log_conflict)
{
    const galera::TrxHandleSlave* const ref_trx(
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
        // 3) Trx has not been certified yet. Already certified trxs show up
        //    here during index rebuild.
        if ((trx->source_id() != ref_trx->source_id() || ref_trx->is_toi()) &&
            ref_seqno > trx->last_seen_seqno() && trx->certified() == false)
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
        const galera::TrxHandleSlave* const ref_shared_trx(
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
           galera::TrxHandleSlave*     const   trx,
           bool                        const   store_keys,
           bool                        const   log_conflicts)
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
        return (!trx->is_toi() &&
                certify_and_depend_v3(kep, key, trx, log_conflicts));
    }
}

// Add key to trx references for trx that passed certification.
//
// @param cert_index certification index in use
// @param trx        certified transaction
// @param key_set    key_set used in certification
// @param key_count  number of keys in key set
static void do_ref_keys(galera::Certification::CertIndexNG& cert_index,
                        galera::TrxHandleSlave*       const trx,
                        const galera::KeySetIn&             key_set,
                        const long                          key_count)
{
    for (long i(0); i < key_count; ++i)
    {
        const galera::KeySet::KeyPart& k(key_set.next());
        galera::KeyEntryNG ke(k);
        galera::Certification::CertIndexNG::const_iterator
            ci(cert_index.find(&ke));

        if (ci == cert_index.end())
        {
            gu_throw_fatal << "could not find key '" << k
                           << "' from cert index";
        }
        (*ci)->ref(k.prefix(), k, trx);
    }
}

// Clean up keys from index that were added by trx that failed
// certification.
//
// @param cert_index certification inde
// @param key_set    key_set used in certification
// @param processed  number of keys that were processed in certification
static void do_clean_keys(galera::Certification::CertIndexNG& cert_index,
                          const galera::KeySetIn&             key_set,
                          const long                          processed)
{
    /* 'strictly less' comparison is essential in the following loop:
     * processed key failed cert and was not added to index */
    for (long i(0); i < processed; ++i)
    {
        KeyEntryNG ke(key_set.next());

        // Clean up cert_index_ from entries which were added by this trx
        galera::Certification::CertIndexNG::iterator ci(cert_index.find(&ke));

        if (gu_likely(ci != cert_index.end()))
        {
            galera::KeyEntryNG* kep(*ci);

            if (kep->referenced() == false)
            {
                // kel was added to cert_index_ by this trx -
                // remove from cert_index_ and fall through to delete
                cert_index.erase(ci);
            }
            else continue;

            assert(kep->referenced() == false);

            delete kep;
        }
        else if(ke.key().shared())
        {
            assert(0); // we actually should never be here, the key should
            // be either added to cert_index_ or be there already
            log_warn  << "could not find shared key '"
                      << ke.key() << "' from cert index";
        }
        else { /* exclusive can duplicate shared */ }
    }
}

galera::Certification::TestResult
galera::Certification::do_test_v3(TrxHandleSlave* const trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION v3: " << *trx;

#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index_
    // to original size
    size_t prev_cert_index_size(cert_index_ng_.size());
#endif // NDEBUG

    const KeySetIn& key_set(trx->write_set().keyset());
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
        do_ref_keys(cert_index_ng_, trx, key_set, key_count);

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
        do_clean_keys(cert_index_ng_, key_set, processed);
        assert(cert_index_ng_.size() == prev_cert_index_size);
    }

    return TEST_FAILED;
}

galera::Certification::TestResult
galera::Certification::do_test(const TrxHandleSlavePtr& trx, bool store_keys)
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

    // trx->is_certified() == true during index rebuild from IST, do_test()
    // must not fail, just populate index
    if (gu_unlikely(trx->certified() == false &&
                    (trx->last_seen_seqno() < initial_position_ ||
                     trx->global_seqno()-trx->last_seen_seqno() > max_length_)))
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

    gu::Lock lock(mutex_); // why do we need that? - e.g. set_trx_committed()

    /* initialize parent seqno */
    if (gu_unlikely(trx_map_.empty()))
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
    case 4:
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
        index_size_ = (cert_index_.size() + cert_index_ng_.size());
    }

    byte_count_ += trx->size();

    return res;
}


galera::Certification::TestResult
galera::Certification::do_test_preordered(TrxHandleSlave* trx)
{
    /* Source ID is not always available for preordered events (e.g. event
     * producer didn't provide any) so for now we must accept undefined IDs. */
    //assert(trx->source_id() != WSREP_UUID_UNDEFINED);

    assert(trx->version() >= 3);
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

    trx->set_depends_seqno(last_preordered_seqno_ + 1 -
                           trx->write_set().pa_range());
    // +1 compensates for subtracting from a previous seqno, rather than own.
    trx->mark_certified();

    last_preordered_seqno_ = trx->global_seqno();
    last_preordered_id_    = trx->trx_id();

    return TEST_OK;
}


galera::Certification::Certification(gu::Config& conf, ServiceThd* thd)
    :
    version_               (-1),
    trx_map_               (),
    cert_index_            (),
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
    log_conflicts_         (conf.get<bool>(CERT_PARAM_LOG_CONFLICTS)),
    current_view_          ()
{}


galera::Certification::~Certification()
{
    log_info << "cert index usage at exit "   << cert_index_.size();
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
    if (service_thd_)
    {
        service_thd_->release_seqno(position_);
        service_thd_->flush(gu::UUID());
    }
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
    case 4:
        break;
    default:
        gu_throw_fatal << "certification/trx version "
                       << version << " not supported";
    }

    gu::Lock lock(mutex_);

    std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
    trx_map_.clear();
    assert(cert_index_.empty());
    assert(cert_index_ng_.empty());

    if (service_thd_)
    {
        service_thd_->release_seqno(position_);
        service_thd_->flush(gtid.uuid());
    }

    log_info << "####### Assign initial position for certification: " << gtid
             << ", protocol version: " << version;

    wsrep_seqno_t const seqno(gtid.seqno());
    initial_position_      = seqno;
    position_              = seqno;
    safe_to_discard_seqno_ = seqno;
    last_pa_unsafe_        = seqno;
    last_preordered_seqno_ = position_;
    last_preordered_id_    = 0;
    version_               = version;
}


void
galera::Certification::adjust_position(const View&         view,
                                       const gu::GTID&     gtid,
                                       int           const version)
{
    assert(gtid.uuid()  != GU_UUID_NIL);
    assert(gtid.seqno() >= 0);

    gu::Lock lock(mutex_);

// this assert is too strong: local ordered transactions may get canceled without
// entering certification    assert(position_ + 1 == seqno || 0 == position_);

    log_info << "####### Adjusting cert position to " << gtid;

    if (version != version_)
    {
        std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
        assert(trx_map_.end()->first + 1 == position_);
        trx_map_.clear();
        assert(cert_index_.empty());
        assert(cert_index_ng_.empty());
        if (service_thd_)
        {
            service_thd_->release_seqno(position_);
        }
    }

    if (service_thd_)
    {
        service_thd_->flush(gtid.uuid());
    }

    position_     = gtid.seqno();
    version_      = version;
    current_view_ = view;
}

galera::Certification::TestResult
galera::Certification::test(const TrxHandleSlavePtr& trx, bool store_keys)
{
    assert(trx->global_seqno() >= 0 /* && trx->local_seqno() >= 0 */);

    const TestResult ret
        (trx->preordered() ?
         do_test_preordered(trx.get()) : do_test(trx, store_keys));

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

    if (handle_gcache && service_thd_) service_thd_->release_seqno(seqno);

    if (0 == ((trx_map_.size() + 1) % 10000))
    {
        log_debug << "trx map after purge: length: " << trx_map_.size()
                  << ", requested purge seqno: " << seqno
                  << ", real purge seqno: " << trx_map_.begin()->first - 1;
    }

    return seqno;
}


galera::Certification::TestResult
galera::Certification::append_trx(const TrxHandleSlavePtr& trx)
{
// explicit ROLLBACK is dummy()    assert(!trx->is_dummy());
    assert(trx->global_seqno() >= 0 /* && trx->local_seqno() >= 0 */);
    assert(trx->global_seqno() > position_);

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

    const TestResult retval(test(trx, true));

    {
        assert(trx->global_seqno() > 0);

        gu::Lock lock(mutex_);
        if (trx_map_.insert(
                std::make_pair(trx->global_seqno(), trx)).second == false)
            gu_throw_fatal << "duplicate trx entry " << *trx;

        // trx with local seqno WSREP_SEQNO_UNDEFINED originates from
        // IST so deps set tracking should not be done
        if (trx->local_seqno() != WSREP_SEQNO_UNDEFINED)
        {
            assert(trx->last_seen_seqno() != WSREP_SEQNO_UNDEFINED);
            deps_set_.insert(trx->last_seen_seqno());
            assert(deps_set_.size() <= trx_map_.size());
        }
    }

    if (!trx->certified()) trx->mark_certified();

    return retval;
}


wsrep_seqno_t galera::Certification::set_trx_committed(TrxHandleSlave& trx)
{
    assert(trx.global_seqno() >= 0);
    assert(trx.is_committed() == false);

    wsrep_seqno_t ret(WSREP_SEQNO_UNDEFINED);
    {
        gu::Lock lock(mutex_);

        // certified trx with local seqno WSREP_SEQNO_UNDEFINED originates from
        // IST so deps set tracking should not be done
        // Local explicit rollback events bypassed certificaiton
        if (trx.certified()   == true &&
            trx.local_seqno() != WSREP_SEQNO_UNDEFINED &&
            !trx.cert_bypass())
        {
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

