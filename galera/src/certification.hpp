
#ifndef GALERA_CERTIFICATION_HPP
#define GALERA_CERTIFICATION_HPP

#include "trx_handle.hpp"

#include <string>

namespace galera
{
    class Certification
    {
    public:
        virtual int append_write_set(TrxHandlePtr);
        virtual int test(wsdb_write_set*, bool);
        virtual wsrep_seqno_t get_safe_to_discard_seqno() const;
        virtual void purge_trxs_upto(wsrep_seqno_t seqno);
        virtual void set_trx_committed(wsrep_seqno_t seqno);
        virtual TrxHandlePtr get_trx(wsrep_seqno_t seqno);
        virtual void deref_seqno(wsrep_seqno_t seqno);
        
        virtual ~Certification() { }
        
        // Factory method
        static Certification* create(const std::string& conf);
    private:
        Certification() { }
        Certification(const Certification&);
        void operator=(const Certification&);
    };
}





#endif // GALERA_CERTIFICATION_HPP
