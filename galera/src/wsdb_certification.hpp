//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WSDB_CERTIFICATION_HPP
#define GALERA_WSDB_CERTIFICATION_HPP

#include "certification.hpp"
#include "gu_mutex.hpp"

#include <map>

namespace galera
{
    // WSDB based certification implementation
    class WsdbCertification : public Certification
    {
    private:
        typedef std::map<wsrep_seqno_t, TrxHandlePtr> TrxMap;
    public:
        WsdbCertification() : trx_map_(), mutex_() { }

        void assign_initial_position(wsrep_seqno_t seqno);
        TrxHandlePtr create_trx(const void* data, size_t data_len,
                                wsrep_seqno_t seqno_l,
                                wsrep_seqno_t seqno_g);
        int append_trx(const TrxHandlePtr&);
        int test(const TrxHandlePtr&, bool);
        wsrep_seqno_t get_safe_to_discard_seqno() const;
        void purge_trxs_upto(wsrep_seqno_t);
        void set_trx_committed(const TrxHandlePtr&);
        TrxHandlePtr get_trx(wsrep_seqno_t);
        void deref_seqno(wsrep_seqno_t);
    private:
        TrxMap trx_map_;
        gu::Mutex  mutex_;
    };
}

#endif // GALERA_WSDB_CERTIFICATION_HPP
