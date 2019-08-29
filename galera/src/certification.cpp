//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#include "certification.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>
#include <algorithm> // std::for_each


using namespace galera;

static const bool cert_debug_on(false);
#define cert_debug                              \
    if (cert_debug_on == false) { }             \
    else log_info << "cert debug: "

#define CERT_PARAM_LOG_CONFLICTS galera::Certification::PARAM_LOG_CONFLICTS
#define CERT_PARAM_OPTIMISTIC_PA galera::Certification::PARAM_OPTIMISTIC_PA

static std::string const CERT_PARAM_PREFIX("cert.");

std::string const CERT_PARAM_LOG_CONFLICTS(CERT_PARAM_PREFIX + "log_conflicts");
std::string const CERT_PARAM_OPTIMISTIC_PA(CERT_PARAM_PREFIX + "optimistic_pa");

static std::string const CERT_PARAM_MAX_LENGTH   (CERT_PARAM_PREFIX +
                                                  "max_length");
static std::string const CERT_PARAM_LENGTH_CHECK (CERT_PARAM_PREFIX +
                                                  "length_check");

static std::string const CERT_PARAM_LOG_CONFLICTS_DEFAULT("no");
static std::string const CERT_PARAM_OPTIMISTIC_PA_DEFAULT("yes");

/*** It is EXTREMELY important that these constants are the same on all nodes.
 *** Don't change them ever!!! ***/
static std::string const CERT_PARAM_MAX_LENGTH_DEFAULT("16384");
static std::string const CERT_PARAM_LENGTH_CHECK_DEFAULT("127");

void
galera::Certification::register_params(gu::Config& cnf)
{
    cnf.add(CERT_PARAM_LOG_CONFLICTS, CERT_PARAM_LOG_CONFLICTS_DEFAULT);
    cnf.add(CERT_PARAM_OPTIMISTIC_PA, CERT_PARAM_OPTIMISTIC_PA_DEFAULT);
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
        const galera::KeySet::KeyPart& kp(key_set.next());
        galera::KeyEntryNG ke(kp);
        galera::Certification::CertIndexNG::iterator ci(cert_index.find(&ke));
        assert(ci != cert_index.end());
        if (ci == cert_index.end())
        {
            log_warn << "Could not find key from index";
            continue;
        }
        galera::KeyEntryNG* const kep(*ci);
        assert(kep->referenced() == true);

        wsrep_key_type_t const p(kp.wsrep_type(ts->version()));

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
    assert(trx->version() >= 3 || trx->version() <= 5);
    const KeySetIn& keys(trx->write_set().keyset());
    keys.rewind();
    purge_key_set(cert_index_ng_, trx, keys, keys.count());
}

/* Specifically for chain use in certify_and_depend_v3to5() */
template <wsrep_key_type_t REF_KEY_TYPE>
bool
check_against(const galera::KeyEntryNG*   const found,
              const galera::KeySet::KeyPart&    key,
              wsrep_key_type_t            const key_type,
              galera::TrxHandleSlave*     const trx,
              bool                        const log_conflict,
              wsrep_seqno_t&                    depends_seqno)
{
    enum CheckType
    {
        CONFLICT,
        DEPENDENCY,
        NOTHING
    };

    static CheckType const check_table
        [WSREP_KEY_EXCLUSIVE+1][WSREP_KEY_EXCLUSIVE+1] =
        {
            //  SH          RE            UP         EX   second / first
            { NOTHING,    NOTHING,    DEPENDENCY, DEPENDENCY },  // SH
            { NOTHING,    NOTHING,    DEPENDENCY, CONFLICT   },  // RE
            { DEPENDENCY, DEPENDENCY, CONFLICT,   CONFLICT   },  // UP
            { CONFLICT,   CONFLICT,   CONFLICT,   CONFLICT   }   // EX
        };

    const galera::TrxHandleSlave* const ref_trx(found->ref_trx(REF_KEY_TYPE));

    // trx should not have any references in index at this point
    assert(ref_trx != trx);

    bool conflict(false);

    if (gu_likely(0 != ref_trx))
    {
        if ((REF_KEY_TYPE == WSREP_KEY_EXCLUSIVE ||
             REF_KEY_TYPE == WSREP_KEY_UPDATE) && ref_trx)
        {
            cert_debug << KeySet::type(REF_KEY_TYPE) << " match: "
                       << *trx << " <---> " << *ref_trx;
        }

        if (REF_KEY_TYPE == WSREP_KEY_SHARED    ||
            REF_KEY_TYPE == WSREP_KEY_REFERENCE ||
            REF_KEY_TYPE == WSREP_KEY_UPDATE) assert(!ref_trx->is_toi());

        CheckType const check_type(check_table[REF_KEY_TYPE][key_type]);

        switch (check_type)
        {
        case CONFLICT:
            // cert conflict takes place if
            // 1) write sets originated from different nodes, are within cert
            //    range
            // 2) ref_trx is in isolation mode, write sets are within cert range
            // 3) Trx has not been certified yet. Already certified trxs show up
            //    here during index rebuild.
            conflict = (ref_trx->global_seqno() > trx->last_seen_seqno() &&
                        (ref_trx->is_toi() ||
                         trx->source_id() != ref_trx->source_id()) &&
                        trx->certified() == false);

            if (gu_unlikely(cert_debug_on || (conflict && log_conflict == true)))
            {
                log_info << KeySet::type(key_type) << '-'
                         << KeySet::type(REF_KEY_TYPE)
                         << " trx " << (conflict ? "conflict" : "match")
                         << " for key " << key << ": "
                         << *trx << " <---> " << *ref_trx;
            }
            /* fall through */
        case DEPENDENCY:
            if (conflict)
                depends_seqno = WSREP_SEQNO_UNDEFINED;
            else
                depends_seqno = std::max(ref_trx->global_seqno(), depends_seqno);
            /* fall through */
        case NOTHING:;
        }
    }

    return conflict;
}

/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v3to5(const galera::KeyEntryNG*   const found,
                         const galera::KeySet::KeyPart&    key,
                         galera::TrxHandleSlave*     const trx,
                         bool                        const log_conflict)
{
    wsrep_seqno_t depends_seqno(trx->depends_seqno());
    wsrep_key_type_t const key_type(key.wsrep_type(trx->version()));

    /*
     * The following cascade implements these rules:
     *
     *      | ex | up | re | sh |  <- horizontal axis: trx   key type
     *   ------------------------     vertical   axis: found key type
     *   ex | C  | C  | C  | C  |
     *   ------------------------
     *   up | C  | C  | D  | D  |
     *   ------------------------     C - conflict
     *   re | C  | D  | N  | N  |     D - dependency
     *   ------------------------     N - nothing
     *   sh | D  | D  | N  | N  |
     *   ------------------------
     *
     * Note that depends_seqno is an in/out parameter and is updated on every
     * step.
     */
    if (check_against<WSREP_KEY_EXCLUSIVE>
        (found, key, key_type, trx, log_conflict, depends_seqno) ||
        check_against<WSREP_KEY_UPDATE>
        (found, key, key_type, trx, log_conflict, depends_seqno) ||
        (key_type >= WSREP_KEY_UPDATE &&
         /* exclusive keys must be checked against shared */
         (check_against<WSREP_KEY_REFERENCE>
          (found, key, key_type, trx, log_conflict, depends_seqno) ||
          check_against<WSREP_KEY_SHARED>
          (found, key, key_type, trx, log_conflict, depends_seqno))))
    {
        return true;
    }
    else
    {
        if (depends_seqno > trx->depends_seqno())
            trx->set_depends_seqno(depends_seqno);
        return false;
    }
}

/* returns true on collision, false otherwise */
static bool
certify_v3to5(galera::Certification::CertIndexNG& cert_index_ng,
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
        // Note: For we skip certification for isolated trxs, only
        // cert index and key_list is populated.
        return (!trx->is_toi() &&
                certify_and_depend_v3to5(kep, key, trx, log_conflicts));
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
        (*ci)->ref(k.wsrep_type(trx->version()), k, trx);
    }
}

// Clean up keys from index that were added by trx that failed
// certification.
//
// @param cert_index certification inde
// @param key_set    key_set used in certification
// @param processed  number of keys that were processed in certification
static void do_clean_keys(galera::Certification::CertIndexNG& cert_index,
                          const galera::TrxHandleSlave* const trx,
                          const galera::KeySetIn&             key_set,
                          const long                          processed)
{
    /* 'strictly less' comparison is essential in the following loop:
     * processed key failed cert and was not added to index */
    for (long i(0); i < processed; ++i)
    {
        KeyEntryNG ke(key_set.next());

        // Clean up cert index from entries which were added by this trx
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
        else if(ke.key().wsrep_type(trx->version()) == WSREP_KEY_SHARED)
        {
            assert(0); // we actually should never be here, the key should
                       // be either added to cert_index_ or be there already
            log_warn  << "could not find shared key '"
                      << ke.key() << "' from cert index";
        }
        else { /* non-shared keys can duplicate shared in the key set */ }
    }
}

galera::Certification::TestResult
galera::Certification::do_test_v3to5(TrxHandleSlave* trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION v" << trx->version() << ": " << *trx;

#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index
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

        if (certify_v3to5(cert_index_ng_, key, trx, store_keys, log_conflicts_))
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
        do_clean_keys(cert_index_ng_, trx, key_set, processed);
        assert(cert_index_ng_.size() == prev_cert_index_size);
    }

    return TEST_FAILED;
}

/* Determine whether a given trx can be correctly certified under the
 * certification protocol currently established in the group (cert_version)
 * Certification protocols from 1 to 3 could only handle writesets of the same
 * version. Certification protocol 4 can handle writesets of both version 3
 * and 4 */
static inline bool
trx_cert_version_match(int const trx_version, int const cert_version)
{
    if (cert_version <= 3)
    {
        return (trx_version == cert_version);
    }
    else
    {
        return (trx_version >= 3 && trx_version <= cert_version);
    }
}

galera::Certification::TestResult
galera::Certification::do_test(const TrxHandleSlavePtr& trx, bool store_keys)
{
    assert(trx->source_id() != WSREP_UUID_UNDEFINED);

    if (!trx_cert_version_match(trx->version(), version_))
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
        if (optimistic_pa_ == false &&
            trx->last_seen_seqno() > trx->depends_seqno())
            trx->set_depends_seqno(trx->last_seen_seqno());

        wsrep_seqno_t const ds(trx_map_.begin()->first - 1);
        if (ds > trx->depends_seqno()) trx->set_depends_seqno(ds);
    }

    switch (version_)
    {
    case 1:
    case 2:
        break;
    case 3:
    case 4:
    case 5:
        res = do_test_v3to5(trx.get(), store_keys);
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

    // Additional NBO certification.
    if (trx->flags() & TrxHandle::F_ISOLATION)
    {
        res = do_test_nbo(trx);
        assert(TEST_FAILED == res || trx->depends_seqno() >= 0);
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


//
// non-blocking operations
//

// Prepare a copy of TrxHandleSlave with private storage
galera::NBOEntry copy_ts(
    galera::TrxHandleSlave* ts,
    galera::TrxHandleSlave::Pool& pool,
    gu::shared_ptr<NBOCtx>::type nbo_ctx)
{
    // FIXME: Pass proper working directory from config to MappedBuffer ctor
    gu::shared_ptr<galera::MappedBuffer>::type buf(
        new galera::MappedBuffer("/tmp"));
    assert(ts->action().first && ts->action().second);
    if (ts->action().first == 0)
    {
        gu_throw_fatal
            << "Unassigned action pointer for transaction, cannot make a copy of: "
            << *ts;
    }

    buf->resize(ts->action().second);
    std::copy(static_cast<const gu::byte_t*>(ts->action().first),
              static_cast<const gu::byte_t*>(ts->action().first)
              + ts->action().second,
              buf->begin());

    galera::TrxHandleSlaveDeleter d;
    gu::shared_ptr<galera::TrxHandleSlave>::type new_ts(
        galera::TrxHandleSlave::New(ts->local(), pool), d);
    if (buf->size() > size_t(std::numeric_limits<int32_t>::max()))
        gu_throw_error(ERANGE) << "Buffer size " << buf->size()
                               << " out of range";
    gcs_action act = {ts->global_seqno(), ts->local_seqno(),
                      &(*buf)[0], static_cast<int32_t>(buf->size()),
                      GCS_ACT_WRITESET};
    if (ts->certified() == false)
    {
        // TrxHandleSlave is from group
        gu_trace(new_ts->unserialize<true>(act));
    }
    else
    {
        // TrxHandleSlave is from IST
        gu_trace(new_ts->unserialize<false>(act));
    }
    new_ts->set_local(ts->local());
    return galera::NBOEntry(new_ts, buf, nbo_ctx);
}


static void purge_key_set_nbo(galera::Certification::CertIndexNBO& cert_index,
                              bool                                 is_nbo_index,
                              galera::TrxHandleSlave*              ts,
                              const galera::KeySetIn&              key_set,
                              const long                           count)
{
    using galera::Certification;
    using galera::KeyEntryNG;
    using galera::KeySet;

    key_set.rewind();

    for (long i(0); i < count; ++i)
    {
        KeyEntryNG ke(key_set.next());
        std::pair<Certification::CertIndexNBO::iterator,
                  Certification::CertIndexNBO::iterator>
            ci_range(cert_index.equal_range(&ke));

        assert(std::distance(ci_range.first, ci_range.second) >= 1);

        wsrep_key_type_t const p(ke.key().wsrep_type(ts->version()));
        Certification::CertIndexNBO::iterator ci;
        for (ci = ci_range.first; ci != ci_range.second; ++ci)
        {
            if ((*ci)->ref_trx(p) == ts) break;
        }
        assert(ci != ci_range.second);
        if (ci == ci_range.second)
        {
            log_warn << "purge_key_set_nbo(): Could not find key "
                     << ke.key() << " from NBO index, skipping";
            continue;
        }

        KeyEntryNG* const kep(*ci);
        assert(kep->referenced() == true);

        kep->unref(p, ts);
        assert(kep->referenced() == false);
        cert_index.erase(ci);
        delete kep;
    }
}


static void end_nbo(galera::NBOMap::iterator             i,
                    galera::TrxHandleSlavePtr            ts,
                    galera::Certification::CertIndexNBO& nbo_index,
                    galera::NBOMap&                      nbo_map)
{
    NBOEntry& e(i->second);

    log_debug << "Ending NBO started by " << *e.ts_ptr();

    // Erase entry from index
    const KeySetIn& key_set(e.ts_ptr()->write_set().keyset());
    purge_key_set_nbo(nbo_index, true, e.ts_ptr(), key_set, key_set.count());

    ts->set_ends_nbo(e.ts_ptr()->global_seqno());

    nbo_map.erase(i);
}


gu::shared_ptr<NBOCtx>::type galera::Certification::nbo_ctx_unlocked(
    wsrep_seqno_t const seqno)
{
    // This will either
    // * Insert a new NBOCtx shared_ptr into ctx map if one didn't exist
    //   before, or
    // * Return existing entry, while newly created shared ptr gets freed
    //   automatically when it goes out of scope
    return nbo_ctx_map_.insert(
        std::make_pair(seqno,
                       gu::make_shared<NBOCtx>())).first->second;
}

gu::shared_ptr<NBOCtx>::type galera::Certification::nbo_ctx(
    wsrep_seqno_t const seqno)
{
    assert(seqno > 0);
    gu::Lock lock(mutex_);
    return nbo_ctx_unlocked(seqno);
}

void galera::Certification::erase_nbo_ctx(wsrep_seqno_t const seqno)
{
    assert(seqno > 0);
    gu::Lock lock(mutex_);

    size_t n_erased(nbo_ctx_map_.erase(seqno));
    assert(n_erased == 1); (void)n_erased;
}


static bool is_exclusive(const galera::KeyEntryNG* ke)
{
    assert(ke != 0);
    assert((ke->ref_trx(WSREP_KEY_SHARED) ||
            ke->ref_trx(WSREP_KEY_REFERENCE) ||
            ke->ref_trx(WSREP_KEY_UPDATE) ||
            ke->ref_trx(WSREP_KEY_EXCLUSIVE)) &&
           !(ke->ref_trx(WSREP_KEY_SHARED) &&
             ke->ref_trx(WSREP_KEY_REFERENCE) &&
             ke->ref_trx(WSREP_KEY_UPDATE) &&
             ke->ref_trx(WSREP_KEY_EXCLUSIVE)));
    return (ke->ref_trx(WSREP_KEY_EXCLUSIVE) != 0 ||
            ke->ref_trx(WSREP_KEY_UPDATE) != 0);
}

static bool
certify_nbo(galera::Certification::CertIndexNBO& cert_index,
            const galera::KeySet::KeyPart&       key,
            galera::TrxHandleSlave* const        trx,
            bool                    const        log_conflicts)
{
    using galera::KeyEntryNG;
    using galera::Certification;
    using galera::TrxHandleSlave;

    KeyEntryNG ke(key);
    std::pair<Certification::CertIndexNBO::iterator,
              Certification::CertIndexNBO::iterator>
        it(cert_index.equal_range(&ke));

    // Certification is done over whole index as opposed to regular
    // write set certification where only given range is used

    // If found range is non-empty, it must be either single exclusive
    // key or all shared.
    assert(std::count_if(it.first, it.second, is_exclusive) == 0 ||
           std::distance(it.first, it.second) == 1);

    Certification::CertIndexNBO::iterator i;
    if ((i = std::find_if(it.first, it.second, is_exclusive)) != cert_index.end())
    {
        if (gu_unlikely(log_conflicts == true))
        {
            const TrxHandleSlave* ref_trx((*i)->ref_trx(WSREP_KEY_EXCLUSIVE));
            assert(ref_trx != 0);
            log_info << "NBO conflict for key " << key << ": "
                     << *trx << " <--X--> " << *ref_trx;
        }
        return true;
    }
    return false;
}

static void
do_ref_keys_nbo(galera::Certification::CertIndexNBO& index,
                TrxHandleSlave*                const trx,
                const galera::KeySetIn&              key_set,
                const long                           key_count)
{
    using galera::KeySet;
    using galera::KeyEntryNG;
    using galera::Certification;

    key_set.rewind();

    for (long i(0); i < key_count; ++i)
    {
        const KeySet::KeyPart& key(key_set.next());
        wsrep_key_type_t const type(key.wsrep_type(trx->version()));
        KeyEntryNG* kep (new KeyEntryNG(key));
        Certification::CertIndexNBO::iterator it;
        assert((it = index.find(kep)) == index.end() ||
               (*it)->ref_trx(type) != trx);
        it = index.insert(kep);
        (*it)->ref(type, key, trx);
    }
}


galera::Certification::TestResult galera::Certification::do_test_nbo(
    const TrxHandleSlavePtr& ts)
{
    assert(!ts->is_dummy());
    assert(ts->flags() & TrxHandle::F_ISOLATION);
    assert(ts->flags() & (TrxHandle::F_BEGIN | TrxHandle::F_COMMIT));

    if (nbo_position_ >= ts->global_seqno())
    {
        // This is part of cert index preload, needs to be dropped since
        // it is already processed by this node before partitioning.
        assert(ts->certified() == true);
        // Return TEST_OK. If the trx is in index preload, it has
        // passed certification on donor.
        log_debug << "Dropping NBO " << *ts;
        return TEST_OK;
    }
    nbo_position_ = ts->global_seqno();

#ifndef NDEBUG
    size_t prev_nbo_index_size(nbo_index_.size());
#endif // NDEBUG

    bool ineffective(false);

    galera::Certification::TestResult ret(TEST_OK);
    if ((ts->flags() & TrxHandle::F_BEGIN) &&
        (ts->flags() & TrxHandle::F_COMMIT))
    {
        // Old school atomic TOI
        log_debug << "TOI: " << *ts;
        const KeySetIn& key_set(ts->write_set().keyset());
        long const      key_count(key_set.count());
        long            processed(0);

        key_set.rewind();
        for (; processed < key_count; ++processed)
        {
            const KeySet::KeyPart& key(key_set.next());
            if (certify_nbo(nbo_index_, key, ts.get(), log_conflicts_))
            {
                ret = TEST_FAILED;
                break;
            }
        }
        log_debug << "NBO test result " << ret << " for TOI " << *ts;
        // Atomic TOI should not cause change in NBO index
        assert(prev_nbo_index_size == nbo_index_.size());
    }
    else if (ts->flags() & TrxHandle::F_BEGIN)
    {
        // Beginning of non-blocking operation
        log_debug << "NBO start: " << *ts;
        // We need a copy of ts since the lifetime of NBO may exceed
        // the lifetime of the buffer in GCache
        NBOEntry entry(copy_ts(ts.get(), nbo_pool_, nbo_ctx_unlocked(
                                   ts->global_seqno())));

        TrxHandleSlave* new_ts(entry.ts_ptr());
        const KeySetIn& key_set(new_ts->write_set().keyset());
        long const      key_count(key_set.count());
        long            processed(0);

        key_set.rewind();
        for (; processed < key_count; ++processed)
        {
            const KeySet::KeyPart& key(key_set.next());
            if (certify_nbo(nbo_index_, key, new_ts, log_conflicts_))
            {
                ret = TEST_FAILED;
                break;
            }
        }

        switch (ret)
        {
        case TEST_OK:
            do_ref_keys_nbo(nbo_index_, new_ts, key_set, key_count);
            nbo_map_.insert(std::make_pair(new_ts->global_seqno(),
                                           entry));
            break;
        case TEST_FAILED:
            // Clean keys not needed here since certify_nbo()
            // does not insert them into nbo_index_
            break;
        }
    }
    else
    {
        assert(ts->nbo_end());
        // End of non-blocking operation
        log_debug << "NBO end: " << *ts;
        ineffective = true;

        NBOKey key;
        const DataSetIn& ws(ts->write_set().dataset());
        ws.rewind();
        assert(ws.count() == 1);
        if (ws.count() != 1) gu_throw_fatal << "Invalid dataset count in "
                                            << *ts;
        gu::Buf buf(ws.next());
        key.unserialize(static_cast<const gu::byte_t*>(buf.ptr), buf.size, 0);

        NBOMap::iterator i(nbo_map_.find(key));
        if (i != nbo_map_.end())
        {
            NBOEntry& e(i->second);
            e.add_ended(ts->source_id());
            if (ts->local() == true)
            {
                // Clear NBO context aborted flag if it is set by
                // intermediate view change.
                e.nbo_ctx()->set_aborted(false);
            }

            if (current_view_.subset_of(e.ended_set()))
            {
                // All nodes of the current primary view have
                // ended the operation.
                end_nbo(i, ts, nbo_index_, nbo_map_);
                ineffective = false;
            }
        }
        else
        {
            log_warn << "no corresponding NBO begin found for NBO end " << *ts;
        }
    }

    if (gu_likely(TEST_OK == ret))
    {
        ts->set_depends_seqno(ts->global_seqno() - 1);
        if (gu_unlikely(ineffective))
        {
            assert(ts->nbo_end());
            assert(ts->ends_nbo() == WSREP_SEQNO_UNDEFINED);
            ret = TEST_FAILED;
        }
    }

    assert(TEST_FAILED == ret || ts->depends_seqno() >= 0);

    return ret;
}

galera::Certification::Certification(gu::Config& conf, ServiceThd* thd)
    :
    version_               (-1),
    conf_                  (conf),
    trx_map_               (),
    cert_index_ng_         (),
    nbo_map_               (),
    nbo_ctx_map_           (),
    nbo_index_             (),
    nbo_pool_              (sizeof(TrxHandleSlave)),
    deps_set_              (),
    current_view_          (),
    service_thd_           (thd),
    mutex_                 (),
    trx_size_warn_count_   (0),
    initial_position_      (-1),
    position_              (-1),
    nbo_position_          (-1),
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
    inconsistent_          (false),
    log_conflicts_         (conf.get<bool>(CERT_PARAM_LOG_CONFLICTS)),
    optimistic_pa_         (conf.get<bool>(CERT_PARAM_OPTIMISTIC_PA))
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
    nbo_map_.clear();
    if (service_thd_)
    {
        service_thd_->release_seqno(position_);
        service_thd_->flush(gu::UUID());
    }
}


void galera::Certification::assign_initial_position(const gu::GTID& gtid,
                                                    int const       version)
{
    assert(gtid.seqno() >= 0 || gtid == gu::GTID());
    switch (version)
    {
        // value -1 used in initialization when trx protocol version is not
        // available
    case -1:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
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
        if (seqno > 0) // don't warn on index reset.
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

    if (service_thd_)
    {
        service_thd_->release_seqno(position_);
        service_thd_->flush(gtid.uuid());
    }

    log_info << "####### Assign initial position for certification: " << gtid
             << ", protocol version: " << version;

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

    log_info << "####### Adjusting cert position: "
             << position_ << " -> " << gtid.seqno();

    if (version != version_)
    {
        std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
        assert(trx_map_.empty() || trx_map_.rbegin()->first + 1 == position_);
        trx_map_.clear();
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

    position_       = gtid.seqno();
    last_pa_unsafe_ = position_;
    version_        = version;
    current_view_   = view;

    // Loop over NBO entries, clear state and abort waiters. NBO end waiters
    // must resend end messages.
    for (NBOMap::iterator i(nbo_map_.begin()); i != nbo_map_.end(); ++i)
    {
        NBOEntry& e(i->second);
        e.clear_ended();
        e.nbo_ctx()->set_aborted(true);
    }
}

wsrep_seqno_t
galera::Certification::increment_position()
{
    gu::Lock lock(mutex_);
    position_++;
    return position_;
}

galera::Certification::TestResult
galera::Certification::test(const TrxHandleSlavePtr& trx, bool store_keys)
{
    assert(trx->global_seqno() >= 0 /* && trx->local_seqno() >= 0 */);

    const TestResult ret
        (trx->preordered() ?
         do_test_preordered(trx.get()) : do_test(trx, store_keys));

    assert(TEST_FAILED == ret || trx->depends_seqno() >= 0);

    if (gu_unlikely(ret != TEST_OK)) { trx->mark_dummy(__LINE__); }

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

#ifndef NDEBUG
    bool const explicit_rollback(trx->explicit_rollback());
#endif /* NDEBUG */

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

#ifndef NDEBUG
    if (explicit_rollback)
    {
        assert(trx->explicit_rollback());
        assert(retval == TEST_OK);
        assert(trx->state() == TrxHandle::S_CERTIFYING);
    }
#endif /* NDEBUG */

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
set_boolean_parameter(bool& param,
                      const std::string& value,
                      const std::string& param_name,
                      const std::string& change_msg)
{
    try
    {
        bool const old(param);
        param = gu::Config::from_config<bool>(value);
        if (old != param)
        {
            log_info << (param ? "Enabled " : "Disabled ") << change_msg;
        }
    }
    catch (gu::NotFound& e)
    {
        gu_throw_error(EINVAL) << "Bad value '" << value
                               << "' for boolean parameter '"
                               << param_name << '\'';
    }
}

void
galera::Certification::param_set(const std::string& key,
                                 const std::string& value)
{
    if (key == Certification::PARAM_LOG_CONFLICTS)
    {
        set_boolean_parameter(log_conflicts_, value, CERT_PARAM_LOG_CONFLICTS,
                              "logging of certification conflicts.");
    }
    else if (key == Certification::PARAM_OPTIMISTIC_PA)
    {
        set_boolean_parameter(optimistic_pa_, value, CERT_PARAM_OPTIMISTIC_PA,
                              "\"optimistic\" parallel applying.");
    }
    else
    {
        throw gu::NotFound();
    }

    conf_.set(key, value);
}

void
galera::Certification::mark_inconsistent()
{
    gu::Lock lock(mutex_);
    assert(!inconsistent_);
    inconsistent_ = true;
}
