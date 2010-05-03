
#ifndef GALERA_GALERA_CERTIFICATION_HPP
#define GALERA_GALERA_CERTIFICATION_HPP

#include "certification.hpp"

#include <map>

namespace galera
{
    class GaleraCertification : public Certification
    {
    private:
        typedef std::map<wsrep_seqno_t, TrxHandle*> TrxMap;
    public:

        GaleraCertification(const std::string& conf);
        ~GaleraCertification();
        void assign_initial_position(wsrep_seqno_t seqno);
        TrxHandle* create_trx(const void* data, size_t data_len,
                              wsrep_seqno_t seqno_l,
                              wsrep_seqno_t seqno_g);
        int append_trx(TrxHandle*);
        int test(const TrxHandle*, bool = true);
        wsrep_seqno_t get_safe_to_discard_seqno() const;
        void purge_trxs_upto(wsrep_seqno_t);
        void set_trx_committed(TrxHandle*);
        TrxHandle* get_trx(wsrep_seqno_t);
    private:
        TrxMap        trx_map_;
        gu::Mutex     mutex_;
        size_t        trx_size_warn_count_;
        wsrep_seqno_t position_;
        wsrep_seqno_t last_committed_;
    };
}

#endif // GALERA_GALERA_CERTIFICATION_HPP
