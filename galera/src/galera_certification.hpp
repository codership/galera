
#ifndef GALERA_GALERA_CERTIFICATION_HPP
#define GALERA_GALERA_CERTIFICATION_HPP

#include "certification.hpp"

#include <map>

#include <boost/unordered_map.hpp>

namespace galera
{
    class GaleraCertification : public Certification
    {
    private:
        class RowKeyHash
        {
        public:
            size_t operator()(const RowKey& rk) const
            {
                const gu::byte_t* b(reinterpret_cast<const gu::byte_t*>(
                                        rk.get_key()));
                const gu::byte_t* e(reinterpret_cast<const gu::byte_t*>(
                                        rk.get_key()) + rk.get_key_len());
                return boost::hash_range(b, e);
            }
        };
        typedef boost::unordered_map<RowKey, RowKeyEntry*, RowKeyHash> CertIndex;
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
    private:
        bool same_source(const TrxHandle*, const TrxHandle*);
        int do_test(TrxHandle*);
        void purge_for_trx(TrxHandle*);
        class PurgeAndDiscard
        {
        public:
            PurgeAndDiscard(GaleraCertification* cert) : cert_(cert) { }
            void operator()(TrxMap::value_type& vt) const
            {
                cert_->purge_for_trx(vt.second);
                vt.second->unref();
            }
            PurgeAndDiscard(const PurgeAndDiscard& other) : cert_(other.cert_) { }
        private:

            void operator=(const PurgeAndDiscard&);
            GaleraCertification* cert_;
        };
        TrxMap        trx_map_;
        CertIndex     cert_index_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t position_;
        wsrep_seqno_t last_committed_;
    };
}

#endif // GALERA_GALERA_CERTIFICATION_HPP
