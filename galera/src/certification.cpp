//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#include "certification.hpp"
#include "uuid.hpp"

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

void
galera::Certification::purge_for_trx_v1to2(TrxHandle* trx)
{
    TrxHandle::CertKeySet& refs(trx->cert_keys_);

    // Unref all referenced and remove if was referenced only by us
    for (TrxHandle::CertKeySet::iterator i = refs.begin(); i != refs.end();
         ++i)
    {
        KeyEntryOS* const kel(i->first);

        const bool full_key(i->second.first);
        const bool shared(i->second.second);

        CertIndex::iterator ci(cert_index_.find(kel));
        assert(ci != cert_index_.end());
        KeyEntryOS* const ke(*ci);

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

void
galera::Certification::purge_for_trx_v3(TrxHandle* trx)
{
    const KeySetIn& keys(trx->write_set_in().keyset());
    keys.rewind();

    // Unref all referenced and remove if was referenced only by us
    for (long i = 0; i < keys.count(); ++i)
    {
        const KeySet::KeyPart& kp(keys.next());

        KeyEntryNG ke(kp);
        CertIndexNG::iterator const ci(cert_index_ng_.find(&ke));

//        assert(ci != cert_index_ng_.end());
        if (gu_unlikely(cert_index_ng_.end() == ci))
        {
            log_warn << "Missing key";
            continue;
        }

        KeyEntryNG* const kep(*ci);
        assert(kep->referenced());

        wsrep_key_type_t const p(kp.wsrep_type(trx->version()));

        if (kep->ref_trx(p) == trx)
        {
            kep->unref(p, trx);

            if (kep->referenced() == false)
            {
                cert_index_ng_.erase(ci);
                delete kep;
            }
        }
    }
}

void
galera::Certification::purge_for_trx(TrxHandle* trx)
{
    if (trx->new_version())
        purge_for_trx_v3(trx);
    else
        purge_for_trx_v1to2(trx);
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


static bool
certify_v1to2(galera::TrxHandle*                trx,
              galera::Certification::CertIndex& cert_index,
              const galera::KeyOS&              key,
              bool const store_keys, bool const log_conflicts)
{
    typedef std::list<galera::KeyPartOS> KPS;

    KPS key_parts(key.key_parts<KPS>());
    KPS::const_iterator begin(key_parts.begin()), end;
    bool full_key(false);
    galera::TrxHandle::CertKeySet& key_list(trx->cert_keys());

    for (end = begin; full_key == false; end != key_parts.end() ? ++end : end)
    {
        full_key = (end == key_parts.end());
        galera::Certification::CertIndex::iterator ci;
        galera::KeyEntryOS ke(key.version(), begin, end, key.flags());

        cert_debug << "key: " << ke.get_key()
                   << " (" << (full_key == true ? "full" : "partial") << ")";

        bool const shared_key(ke.get_key().flags() & galera::KeyOS::F_SHARED);

        if (store_keys && (key_list.find(&ke) != key_list.end()))
        {
            // avoid certification for duplicates
            // should be removed once we can eleminate dups on deserialization
            continue;
        }

        galera::KeyEntryOS* kep;

        if ((ci = cert_index.find(&ke)) == cert_index.end())
        {
            if (store_keys)
            {
                kep = new galera::KeyEntryOS(ke);
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
                    kep = new galera::KeyEntryOS(ke);
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
    cert_debug << "BEGIN CERTIFICATION v1to2: " << *trx;
#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index_
    // to original size
    size_t prev_cert_index_size(cert_index_.size());
#endif // NDEBUG

    galera::TrxHandle::CertKeySet& key_list(trx->cert_keys_);

    long   key_count(0);
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
            KeyOS key(trx->version());
            offset = key.unserialize(buf, buf_len, offset);
            if (certify_v1to2(trx,
                              cert_index_,
                              key,
                              store_keys,
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
            KeyEntryOS* const kel(i->first);
            CertIndex::const_iterator ci(cert_index_.find(kel));

            if (ci == cert_index_.end())
            {
                gu_throw_fatal << "could not find key '"
                               << kel->get_key() << "' from cert index";
            }

            KeyEntryOS* const ke(*ci);
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
            KeyEntryOS* const kel(i->first);

            // Clean up cert_index_ from entries which were added by this trx
            CertIndex::iterator ci(cert_index_.find(kel));

            if (ci != cert_index_.end())
            {
                KeyEntryOS* ke(*ci);

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

/* Specifically for chain use in certify_and_depend_v3to4() */
template <wsrep_key_type_t REF_KEY_TYPE>
bool
check_against(const galera::KeyEntryNG*   const found,
              const galera::KeySet::KeyPart&    key,
              wsrep_key_type_t            const key_type,
              galera::TrxHandle*          const trx,
              bool                        const log_conflict,
              wsrep_seqno_t&                    depends_seqno)
{
    const galera::TrxHandle* const ref_trx(found->ref_trx(REF_KEY_TYPE));

    // trx should not have any references in index at this point
    assert(ref_trx != trx);

    bool conflict(false);

    if (gu_likely(0 != ref_trx))
    {
        if (REF_KEY_TYPE == WSREP_KEY_EXCLUSIVE && ref_trx)
        {
            cert_debug << KeySet::type(REF_KEY_TYPE) << " match: "
                       << *trx << " <-----> " << *ref_trx;
        }

        if (REF_KEY_TYPE == WSREP_KEY_SHARED ||
            REF_KEY_TYPE == WSREP_KEY_SEMI) assert(!ref_trx->is_toi());

        // cert conflict takes place if
        // 1) write sets originated from different nodes, are within cert range
        // 2) ref_trx is in isolation mode, write sets are within cert range
        switch(REF_KEY_TYPE)
        {
        case WSREP_KEY_EXCLUSIVE:
            conflict = ref_trx->is_toi();
            /* fall through */
        case WSREP_KEY_SEMI:
            conflict = (ref_trx->global_seqno() > trx->last_seen_seqno() &&
                        (conflict || trx->source_id() != ref_trx->source_id()));
            /* fall through */
        case WSREP_KEY_SHARED:;
        }

        if (gu_unlikely(cert_debug_on || (conflict && log_conflict == true)))
        {
            log_info << KeySet::type(key_type) << '-'
                     << KeySet::type(REF_KEY_TYPE)
                     << " trx " << (conflict ? "conflict" : "match")
                     << " for key " << key << ": "
                     << *trx << " <---> " << *ref_trx;
        }

        if (conflict)
        {
            depends_seqno = -1;
        }
        else if (key_type     == WSREP_KEY_EXCLUSIVE ||
                 REF_KEY_TYPE == WSREP_KEY_EXCLUSIVE)
        {
            depends_seqno = std::max(ref_trx->global_seqno(), depends_seqno);
        }
    }

    return conflict;
}

/*! for convenience returns true if conflict and false if not */
static inline bool
certify_and_depend_v3to4(const galera::KeyEntryNG*   const found,
                         const galera::KeySet::KeyPart&    key,
                         galera::TrxHandle*          const trx,
                         bool                        const log_conflict)
{
    wsrep_seqno_t depends_seqno(trx->depends_seqno());
    wsrep_key_type_t const key_type(key.wsrep_type(trx->version()));

    /*
     * The following cascade implements these rules:
     *
     *      | ex | ss | sh |  <- horizontal axis: trx   key type
     *   -------------------     vertical   axis: found key type
     *   ex | C  | C  | C  |
     *   -------------------     C - conflict
     *   ss | C  | N  | N  |     D - dependency
     *   -------------------     N - nothing
     *   sh | D  | N  | N  |
     *   -------------------
     *
     * Note that depends_seqno is an in/out parameter and is updated on every
     * step.
     */
    if (check_against<WSREP_KEY_EXCLUSIVE>
        (found, key, key_type, trx, log_conflict, depends_seqno) ||
        (key_type == WSREP_KEY_EXCLUSIVE &&
         /* exclusive keys must be checked against shared */
         (check_against<WSREP_KEY_SEMI>
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
certify_v3to4(galera::Certification::CertIndexNG& cert_index_ng,
              const galera::KeySet::KeyPart&      key,
              galera::TrxHandle*                  trx,
              bool const                          store_keys,
              bool const                          log_conflicts)
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
                certify_and_depend_v3to4(kep, key, trx, log_conflicts));
    }
}

galera::Certification::TestResult
galera::Certification::do_test_v3to4(TrxHandle* trx, bool store_keys)
{
    cert_debug << "BEGIN CERTIFICATION v" << trx->version() << ": " << *trx;

#ifndef NDEBUG
    // to check that cleanup after cert failure returns cert_index_
    // to original size
    size_t prev_cert_index_size(cert_index_.size());
#endif // NDEBUG

    const KeySetIn& key_set(trx->write_set_in().keyset());
    long const      key_count(key_set.count());
    long            processed(0);


    key_set.rewind();

    for (; processed < key_count; ++processed)
    {
        const KeySet::KeyPart& key(key_set.next());

        if (certify_v3to4(cert_index_ng_, key, trx, store_keys, log_conflicts_))
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

            kep->ref(k.wsrep_type(trx->version()), k, trx);

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

            // Clean up cert_index_ from entries which were added by this trx
            CertIndexNG::iterator ci(cert_index_ng_.find(&ke));

            if (gu_likely(ci != cert_index_ng_.end()))
            {
                KeyEntryNG* kep(*ci);

                if (kep->referenced() == false)
                {
                    // kel was added to cert_index_ by this trx -
                    // remove from cert_index_ and fall through to delete
                    cert_index_ng_.erase(ci);
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
        assert(cert_index_.size() == prev_cert_index_size);
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
galera::Certification::do_test(TrxHandle* trx, bool store_keys)
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

    gu::Lock lock(mutex_); // why do we need that? - e.g. set_trx_committed()

    /* initialize parent seqno */
    if ((trx->flags() & (TrxHandle::F_ISOLATION | TrxHandle::F_PA_UNSAFE))
        || trx_map_.empty())
    {
        trx->set_depends_seqno(trx->global_seqno() - 1);
    }
    else
    {
        trx->set_depends_seqno(
            trx_map_.begin()->second->global_seqno() - 1);

        if (optimistic_pa_ == false &&
            trx->last_seen_seqno() > trx->depends_seqno())
            trx->set_depends_seqno(trx->last_seen_seqno());
    }

    switch (version_)
    {
    case 1:
    case 2:
        res = do_test_v1to2(trx, store_keys);
        break;
    case 3:
    case 4:
        res = do_test_v3to4(trx, store_keys);
        break;
    default:
        gu_throw_fatal << "certification test for version "
                       << version_ << " not implemented";
    }

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
galera::Certification::do_test_preordered(TrxHandle* trx)
{
    /* Source ID is not always available for preordered events (e.g. event
     * producer didn't provide any) so for now we must accept undefined IDs. */
    //assert(trx->source_id() != WSREP_UUID_UNDEFINED);

    assert(trx->new_version());
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
    conf_                  (conf),
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
    optimistic_pa_         (conf.get<bool>(CERT_PARAM_OPTIMISTIC_PA))
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
    service_thd_.release_seqno(position_);
    service_thd_.flush();
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
    case 3:
    case 4:
        break;
    default:
        gu_throw_fatal << "certification/trx version "
                       << version << " not supported";
    }

    gu::Lock lock(mutex_);

    if (seqno >= position_)
    {
        std::for_each(trx_map_.begin(), trx_map_.end(), PurgeAndDiscard(*this));
        assert(cert_index_.size() == 0);
        assert(cert_index_ng_.size() == 0);
    }
    else
    {
        log_warn << "moving position backwards: " << position_ << " -> "
                 << seqno;
        std::for_each(cert_index_.begin(), cert_index_.end(),
                      gu::DeleteObject());
        std::for_each(cert_index_ng_.begin(), cert_index_ng_.end(),
                      gu::DeleteObject());
        std::for_each(trx_map_.begin(), trx_map_.end(),
                      Unref2nd<TrxMap::value_type>());
        cert_index_.clear();
        cert_index_ng_.clear();
    }

    trx_map_.clear();

    log_info << "Assign initial position for certification: " << seqno
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
galera::Certification::test(TrxHandle* trx, bool bval)
{
    assert(trx->global_seqno() >= 0 && trx->local_seqno() >= 0);

    const TestResult ret
        (trx->preordered() ? do_test_preordered(trx) : do_test(trx, bval));

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


galera::Certification::TestResult
galera::Certification::append_trx(TrxHandle* trx)
{
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

    {
        gu::Lock lock(mutex_);

        if (trx_map_.insert(
                std::make_pair(trx->global_seqno(), trx)).second == false)
            gu_throw_fatal << "duplicate trx entry " << *trx;

        deps_set_.insert(trx->last_seen_seqno());
        assert(deps_set_.size() <= trx_map_.size());
    }

    trx->mark_certified();

    return retval;
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
