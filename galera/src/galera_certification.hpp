//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_GALERA_CERTIFICATION_HPP
#define GALERA_GALERA_CERTIFICATION_HPP

#include "certification.hpp"
#include "gu_unordered.hpp"
#include "gu_lock.hpp"

#include <map>
#include <set>


namespace galera
{

    class RowKeyEntry
    {
    public:
        RowKeyEntry(const RowKey& row_key);

        const RowKey& get_row_key() const;
        void ref(TrxHandle* trx);
        void unref(TrxHandle* trx);
        TrxHandle* get_ref_trx() const;
        RowKeyEntry(const RowKeyEntry& other)
            :
            row_key_(other.row_key_),
            row_key_buf_(other.row_key_buf_),
            ref_trx_(other.ref_trx_)
        { }

    private:
        void operator=(const RowKeyEntry&);
        RowKey row_key_;
        gu::Buffer row_key_buf_;
        TrxHandle* ref_trx_;
    };

    class GaleraCertification : public Certification
    {
    private:

        typedef gu::UnorderedMap<RowKey, RowKeyEntry*, RowKeyHash> CertIndex;
        class DiscardRK
        {
        public:
            void operator()(CertIndex::value_type& vt) const
            {
                delete vt.second;
            }
        };
        typedef std::multiset<wsrep_seqno_t> DepsSet;
        typedef std::map<wsrep_seqno_t, TrxHandle*> TrxMap;
    public:

        GaleraCertification(const std::string& conf);
        ~GaleraCertification();
        void assign_initial_position(wsrep_seqno_t seqno);
        TrxHandle* create_trx(const void* data, size_t data_len,
                              wsrep_seqno_t seqno_l,
                              wsrep_seqno_t seqno_g);
        int append_trx(TrxHandle*);
        int test(TrxHandle*, bool = true);
        wsrep_seqno_t get_safe_to_discard_seqno() const;
        void purge_trxs_upto(wsrep_seqno_t);
        void set_trx_committed(TrxHandle*);
        TrxHandle* get_trx(wsrep_seqno_t);

        // statistics section
        double get_avg_deps_dist() const
        {
            gu::Lock lock(mutex_);
            return (n_certified_ == 0 ? 0 : double(deps_dist_)/n_certified_);
        }

    private:
        int do_test(TrxHandle*, bool);
        void purge_for_trx(TrxHandle*);

        class PurgeAndDiscard
        {
        public:
            PurgeAndDiscard(GaleraCertification* cert) : cert_(cert) { }
            void operator()(TrxMap::value_type& vt) const
            {
                {
                    TrxHandle* trx(vt.second);
                    TrxHandleLock lock(*trx);
                    assert(trx->is_committed() == true);
                    cert_->purge_for_trx(trx);
                    if (trx->is_certified() == true)
                    {
                        cert_->n_certified_--;
                        cert_->deps_dist_ -= (trx->get_global_seqno() - trx->get_last_seen_seqno());
                    }
                }
                vt.second->unref();
            }
            PurgeAndDiscard(const PurgeAndDiscard& other) : cert_(other.cert_) { }
        private:

            void operator=(const PurgeAndDiscard&);
            GaleraCertification* cert_;
        };

        TrxMap        trx_map_;
        CertIndex     cert_index_;
        DepsSet       deps_set_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t position_;
        wsrep_seqno_t safe_to_discard_seqno_;
        size_t        n_certified_;
        wsrep_seqno_t deps_dist_;
    };
}

#endif // GALERA_GALERA_CERTIFICATION_HPP
