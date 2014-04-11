//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#ifndef GALERA_CERTIFICATION_HPP
#define GALERA_CERTIFICATION_HPP

#include "trx_handle.hpp"
#include "key_entry_ng.hpp"
#include "galera_service_thd.hpp"

#include "gu_unordered.hpp"
#include "gu_lock.hpp"
#include "gu_config.hpp"

#include <map>
#include <set>
#include <list>

namespace galera
{
    class Certification
    {
    public:

        static std::string const PARAM_LOG_CONFLICTS;

        static void register_params(gu::Config&);

        typedef gu::UnorderedSet<KeyEntryOS*,
                                 KeyEntryPtrHash, KeyEntryPtrEqual> CertIndex;

        typedef gu::UnorderedSet<KeyEntryNG*,
                                 KeyEntryPtrHashNG, KeyEntryPtrEqualNG>
        CertIndexNG;

    private:

        typedef std::multiset<wsrep_seqno_t>        DepsSet;

        typedef std::map<wsrep_seqno_t, TrxHandle*> TrxMap;

    public:

        typedef enum
        {
            TEST_OK,
            TEST_FAILED
        } TestResult;

        Certification(gu::Config& conf, ServiceThd& thd);
        ~Certification();

        void assign_initial_position(wsrep_seqno_t seqno, int versiono);
        TestResult append_trx(TrxHandle*);
        TestResult test(TrxHandle*, bool = true);
        wsrep_seqno_t position() const { return position_; }

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
        wsrep_seqno_t set_trx_committed(TrxHandle*);
        TrxHandle* get_trx(wsrep_seqno_t);

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

        bool index_purge_required()
        {
            register long const count(key_count_.fetch_and_zero());
            return ((count > Certification::purge_interval_ ||
                     (trx_map_.size() + 1) % 128 == 0)
                    ||
                    (key_count_ += count /* restore count */, false));
        }

        void set_log_conflicts(const std::string& str);

    private:

        TestResult do_test(TrxHandle*, bool);
        TestResult do_test_v1to2(TrxHandle*, bool);
        TestResult do_test_v3(TrxHandle*, bool);
        TestResult do_test_preordered(TrxHandle*);
        void purge_for_trx(TrxHandle*);
        void purge_for_trx_v1to2(TrxHandle*);
        void purge_for_trx_v3(TrxHandle*);

        // unprotected variants for internal use
        wsrep_seqno_t get_safe_to_discard_seqno_() const;
        wsrep_seqno_t purge_trxs_upto_(wsrep_seqno_t, bool sync);

        class PurgeAndDiscard
        {
        public:

            PurgeAndDiscard(Certification& cert) : cert_(cert) { }

            void operator()(TrxMap::value_type& vt) const
            {
                {
                    TrxHandle* trx(vt.second);
                    TrxHandleLock lock(*trx);

                    if (trx->is_committed() == false)
                    {
                        log_warn << "trx not committed in purge and discard: "
                                 << *trx;
                    }

                    if (trx->depends_seqno() > -1)
                    {
                        cert_.purge_for_trx(trx);
                    }

                    if (trx->refcnt() > 1)
                    {
                        log_debug << "trx "     << trx->trx_id()
                                  << " refcnt " << trx->refcnt();
                    }
                }
                vt.second->unref();
            }

            PurgeAndDiscard(const PurgeAndDiscard& other) : cert_(other.cert_)
            { }

        private:

            void operator=(const PurgeAndDiscard&);
            Certification& cert_;
        };

        int           version_;
        TrxMap        trx_map_;
        CertIndex     cert_index_;
        CertIndexNG   cert_index_ng_;
        DepsSet       deps_set_;
        ServiceThd&   service_thd_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t initial_position_;
        wsrep_seqno_t position_;
        wsrep_seqno_t safe_to_discard_seqno_;
        wsrep_seqno_t last_pa_unsafe_;
        wsrep_seqno_t last_preordered_seqno_;
        wsrep_trx_id_t last_preordered_id_;
        gu::Mutex     stats_mutex_;
        size_t        n_certified_;
        wsrep_seqno_t deps_dist_;
        wsrep_seqno_t cert_interval_;
        size_t        index_size_;

        gu::Atomic<long>    key_count_;

        /* The only reason those are not static constants is because
         * there might be a need to thange them without recompilation.
         * see #454 */
        int          const max_length_; /* Purge trx_map_ when it exceeds this
                                          * NOTE: this effectively sets a limit
                                          * on trx certification interval */

        unsigned int const max_length_check_; /* Mask how often to check */
        static int   const purge_interval_ = (1UL<<10);

        bool               log_conflicts_;
    };
}

#endif // GALERA_CERTIFICATION_HPP
