//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#ifndef GALERA_CERTIFICATION_HPP
#define GALERA_CERTIFICATION_HPP


#include "nbo.hpp"
#include "trx_handle.hpp"
#include "key_entry_ng.hpp"
#include "galera_service_thd.hpp"
#include "galera_view.hpp"

#include <gu_shared_ptr.hpp>
#include <gu_unordered.hpp>
#include <gu_lock.hpp>
#include <gu_config.hpp>
#include <gu_gtid.hpp>

#include <map>
#include <list>

namespace galera
{
    class Certification
    {
    public:

        static std::string const PARAM_LOG_CONFLICTS;
        static std::string const PARAM_OPTIMISTIC_PA;

        static void register_params(gu::Config&);

        typedef gu::UnorderedSet<KeyEntryOS*,
                                 KeyEntryPtrHash, KeyEntryPtrEqual> CertIndex;

        typedef gu::UnorderedSet<KeyEntryNG*,
                                 KeyEntryPtrHashNG, KeyEntryPtrEqualNG>
        CertIndexNG;

        typedef gu::UnorderedMultiset<KeyEntryNG*,
                                      KeyEntryPtrHashNG, KeyEntryPtrEqualNG>
        CertIndexNBO;

    private:

        typedef std::multiset<wsrep_seqno_t>             DepsSet;

        typedef std::map<wsrep_seqno_t, TrxHandleSlavePtr> TrxMap;

    public:

        typedef enum
        {
            TEST_OK,
            TEST_FAILED
        } TestResult;

        Certification(gu::Config& conf, ServiceThd* thd);
        ~Certification();

        void assign_initial_position(const gu::GTID& gtid, int version);
        TestResult append_trx(const TrxHandleSlavePtr&);
        TestResult test(const TrxHandleSlavePtr&, bool store_keys);
        wsrep_seqno_t position() const { return position_; }
        wsrep_seqno_t increment_position(); /* for dummy IST events */

        /* this is for configuration change use */
        void adjust_position(const View&, const gu::GTID& gtid, int version);

        wsrep_seqno_t
        get_safe_to_discard_seqno() const
        {
            gu::Lock lock(mutex_);
            return get_safe_to_discard_seqno_();
        }

        wsrep_seqno_t
        purge_trxs_upto(wsrep_seqno_t const seqno, bool const handle_gcache)
        {
            gu::Lock lock(mutex_);
            const wsrep_seqno_t stds(get_safe_to_discard_seqno_());
            // assert(seqno <= get_safe_to_discard_seqno());
            // Note: setting trx committed is not done in total order so
            // safe to discard seqno may decrease. Enable assertion above when
            // this issue is fixed.
            return purge_trxs_upto_(std::min(seqno, stds), handle_gcache);
        }

        // Set trx corresponding to handle committed. Return purge seqno if
        // index purge is required, -1 otherwise.
        wsrep_seqno_t set_trx_committed(TrxHandleSlave&);

        // statistics section
        void stats_get(double& avg_cert_interval,
                       double& avg_deps_dist,
                       size_t& index_size) const
        {
            gu::Lock lock(stats_mutex_);
            avg_cert_interval = 0;
            avg_deps_dist = 0;
            if (n_certified_)
            {
                avg_cert_interval = double(cert_interval_) / n_certified_;
                avg_deps_dist = double(deps_dist_) / n_certified_;
            }
            index_size = index_size_;
        }

        void stats_reset()
        {
            gu::Lock lock(stats_mutex_);
            cert_interval_ = 0;
            deps_dist_ = 0;
            n_certified_ = 0;
            index_size_ = 0;
        }

        void param_set(const std::string& key, const std::string& value);

        wsrep_seqno_t lowest_trx_seqno() const
        {
            return (trx_map_.empty() ? position_ : trx_map_.begin()->first);
        }

        //
        // NBO context lifecycle:
        // * Context is created when NBO-start event is received
        // * Context stays in nbo_ctx_map_ until client calls erase_nbo_ctx()
        //

        // Get NBO context matching to global seqno
        gu::shared_ptr<NBOCtx>::type nbo_ctx(wsrep_seqno_t);
        // Erase NBO context entry
        void erase_nbo_ctx(wsrep_seqno_t);
        size_t nbo_size() const { return nbo_map_.size(); }

        void mark_inconsistent();
        bool is_inconsistent() const { return inconsistent_; }

    private:

        // Non-copyable
        Certification(const Certification&);
        Certification& operator=(const Certification&);

        TestResult do_test(const TrxHandleSlavePtr&, bool store_keys);
        TestResult do_test_v3to5(TrxHandleSlave*, bool);
        TestResult do_test_preordered(TrxHandleSlave*);
        TestResult do_test_nbo(const TrxHandleSlavePtr&);
        void purge_for_trx(TrxHandleSlave*);

        // unprotected variants for internal use
        wsrep_seqno_t get_safe_to_discard_seqno_() const;
        wsrep_seqno_t purge_trxs_upto_(wsrep_seqno_t, bool sync);

        gu::shared_ptr<NBOCtx>::type nbo_ctx_unlocked(wsrep_seqno_t);

        bool index_purge_required()
        {
            static unsigned int const KEYS_THRESHOLD (1   << 10); // 1K
            static unsigned int const BYTES_THRESHOLD(128 << 20); // 128M
            static unsigned int const TRXS_THRESHOLD (127);

            /* if either key count, byte count or trx count exceed their
             * threshold, zero up counts and return true. */
            return ((key_count_  > KEYS_THRESHOLD  ||
                     byte_count_ > BYTES_THRESHOLD ||
                     trx_count_  > TRXS_THRESHOLD)
                     &&
                     (key_count_ = 0, byte_count_ = 0, trx_count_ = 0, true));
        }

        class PurgeAndDiscard
        {
        public:

            PurgeAndDiscard(Certification& cert) : cert_(cert) { }

            void operator()(TrxMap::value_type& vt) const
            {
                {
                    TrxHandleSlave* trx(vt.second.get());
                    // Trying to lock trx mutex here may cause deadlock
                    // with streaming replication. Locking can be skipped
                    // because trx is only read here and refcount uses atomics.
                    // Memory barrier is provided by certification mutex.
                    //
                    // TrxHandleLock   lock(*trx);

                    if (!cert_.is_inconsistent())
                    {
                        assert(trx->is_committed() == true);
                        if (trx->is_committed() == false)
                        {
                            log_warn <<"trx not committed in purge and discard: "
                                     << *trx;
                        }
                    }

                    // If depends seqno is not WSREP_SEQNO_UNDEFINED
                    // write set certification has passed and keys have been
                    // inserted into index and purge is needed.
                    // TOI write sets will always pass regular certification
                    // and keys will be inserted, however if they fail
                    // NBO certification depends seqno is set to
                    // WSREP_SEQNO_UNDEFINED. Therefore purge should always
                    // be done for TOI write sets.
                    if (trx->depends_seqno() >= 0 || trx->is_toi() == true)
                    {
                        cert_.purge_for_trx(trx);
                    }
                }
            }

            PurgeAndDiscard(const PurgeAndDiscard& other) : cert_(other.cert_)
            { }

        private:

            void operator=(const PurgeAndDiscard&);
            Certification& cert_;
        };

        int           version_;
        gu::Config&   conf_;
        TrxMap        trx_map_;
        CertIndexNG   cert_index_ng_;
        NBOMap        nbo_map_;
        NBOCtxMap     nbo_ctx_map_;
        CertIndexNBO  nbo_index_;
        TrxHandleSlave::Pool nbo_pool_;
        DepsSet       deps_set_;
        View          current_view_;
        ServiceThd*   service_thd_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t initial_position_;
        wsrep_seqno_t position_;
        wsrep_seqno_t nbo_position_;
        wsrep_seqno_t safe_to_discard_seqno_;
        wsrep_seqno_t last_pa_unsafe_;
        wsrep_seqno_t last_preordered_seqno_;
        wsrep_trx_id_t last_preordered_id_;
        gu::Mutex     stats_mutex_;
        size_t        n_certified_;
        wsrep_seqno_t deps_dist_;
        wsrep_seqno_t cert_interval_;
        size_t        index_size_;

        size_t        key_count_;
        size_t        byte_count_;
        size_t        trx_count_;

        /* The only reason those are not static constants is because
         * there might be a need to thange them without recompilation.
         * see #454 */
        int          const max_length_; /* Purge trx_map_ when it exceeds this
                                          * NOTE: this effectively sets a limit
                                          * on trx certification interval */

        unsigned int const max_length_check_; /* Mask how often to check */

        bool               inconsistent_;
        bool               log_conflicts_;
        bool               optimistic_pa_;
    };
}

#endif // GALERA_CERTIFICATION_HPP
