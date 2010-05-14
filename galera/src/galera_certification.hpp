
#ifndef GALERA_GALERA_CERTIFICATION_HPP
#define GALERA_GALERA_CERTIFICATION_HPP

#include "certification.hpp"

#include <map>

#include <boost/unordered_map.hpp>

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

        typedef boost::unordered_map<RowKey, RowKeyEntry*, RowKeyHash> CertIndex;
        typedef std::map<wsrep_seqno_t, TrxHandle*> TrxMap;

        class TrxId
        {
        public:
            TrxId(const wsrep_uuid_t& source_id, 
                  wsrep_conn_id_t conn_id,
                  wsrep_trx_id_t trx_id)
                : 
                source_id_(source_id),
                conn_id_(conn_id),
                trx_id_(trx_id)
            { }
            bool operator==(const TrxId& other) const
            {
                return (trx_id_ == other.trx_id_ &&
                        conn_id_ == other.conn_id_ &&
                        memcmp(&source_id_, &other.source_id_, 
                               sizeof(source_id_)) == 0);
                
            }
            class Hash
            {
            public:
                size_t operator()(const TrxId& trx_id) const
                {
                    return boost::hash_value(trx_id.trx_id_) ^
                        boost::hash_value(trx_id.conn_id_);
                }
            };
        private:
            wsrep_uuid_t source_id_;
            wsrep_conn_id_t conn_id_;
            wsrep_trx_id_t trx_id_;
        };


        typedef boost::unordered_map<TrxId, TrxHandle*, TrxId::Hash> TrxHash;
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
        int do_test(TrxHandle*, bool);
        void purge_for_trx(TrxHandle*);
        class PurgeAndDiscard
        {
        public:
            PurgeAndDiscard(GaleraCertification* cert) : cert_(cert) { }
            void operator()(TrxMap::value_type& vt) const
            {
                {
                    TrxHandleLock lock(*vt.second);
                    if (vt.second->is_local() == false)
                    {
                        cert_->trx_hash_.erase(TrxId(vt.second->get_source_id(),
                                                     vt.second->get_conn_id(),
                                                     vt.second->get_trx_id()));
                    }
                    cert_->purge_for_trx(vt.second);
                }
                vt.second->unref();
            }
            PurgeAndDiscard(const PurgeAndDiscard& other) : cert_(other.cert_) { }
        private:
            
            void operator=(const PurgeAndDiscard&);
            GaleraCertification* cert_;
        };
        TrxMap        trx_map_;
        TrxHash       trx_hash_;
        CertIndex     cert_index_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t position_;
        wsrep_seqno_t last_committed_;
    };
}

#endif // GALERA_GALERA_CERTIFICATION_HPP
