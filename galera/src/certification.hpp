//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
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
        struct Param
        {
#define CERTIFICATION_PARAM_LOG_CONFLICTS_STR "cert.log_conflicts"
            static const std::string log_conflicts;
        };

        struct Defaults
        {
#define CERTIFICATION_DEFAULTS_LOG_CONFLICTS_STR "no"
            static const std::string log_conflicts;
        };

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

        void
        purge_trxs_upto(wsrep_seqno_t seqno)
        {
            gu::Lock lock(mutex_);
            const wsrep_seqno_t stds(get_safe_to_discard_seqno_());
            // assert(seqno <= get_safe_to_discard_seqno());
            // Note: setting trx committed is not done in total order so
            // safe to discard seqno may decrease. Enable assertion above when
            // this issue is fixed.
            purge_trxs_upto_(std::min(seqno, stds));
        }

        // Set trx corresponding to handle committed. Return purge seqno if
        // index purge is required, -1 otherwise.
        wsrep_seqno_t set_trx_committed(TrxHandle*);
        TrxHandle* get_trx(wsrep_seqno_t);

        // statistics section
        double get_avg_deps_dist() const
        {
            gu::Lock lock(mutex_);
            return (n_certified_ == 0 ? 0 : double(deps_dist_)/n_certified_);
        }

        size_t index_size() const
        {
            gu::Lock lock(mutex_);
            return cert_index_.size() + cert_index_ng_.size();
        }

        bool index_purge_required()
        {
            register long const count(key_count_.fetch_and_zero());

            return ((count > Certification::purge_interval_) ||
                    (key_count_ += count /* restore count */, false));
        }

        void set_log_conflicts(const std::string& str);

    private:
        TestResult do_test(TrxHandle*, bool);
        TestResult do_test_v1to2(TrxHandle*, bool);
        TestResult do_test_v3(TrxHandle*, bool);
        void purge_for_trx(TrxHandle*);
        void purge_for_trx_v1to2(TrxHandle*);
        void purge_for_trx_v3(TrxHandle*);

        // unprotected variants for internal use
        wsrep_seqno_t get_safe_to_discard_seqno_() const;
        void          purge_trxs_upto_(wsrep_seqno_t);

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
                        cert_.n_certified_--;
                        cert_.deps_dist_ -=
                            (trx->global_seqno() - trx->depends_seqno());
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
        size_t        n_certified_;
        wsrep_seqno_t deps_dist_;

        /* The only reason those are not static constants is because
         * there might be a need to thange them without recompilation.
         * see #454 */
        long          const max_length_; /* Purge trx_map_ when it exceeds this
                                          * NOTE: this effectively sets a limit
                                          * on trx certification interval */
        static long   const max_length_default;

        unsigned long const max_length_check_; /* Mask how often to check */
        static unsigned long  const max_length_check_default;

        bool                log_conflicts_;

        static long   const purge_interval_ = (1UL<<10);
        gu::Atomic<long>    key_count_;
    };
}

#endif // GALERA_CERTIFICATION_HPP
