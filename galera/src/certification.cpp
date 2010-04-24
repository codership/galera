
#include "certification.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>

using namespace std;
using namespace gu;

namespace galera
{
    // Default WSDB based certification implementation
    class WsdbCertification : public Certification
    {
    private:
        typedef map<wsrep_seqno_t, TrxHandlePtr> TrxMap;
    public:
        WsdbCertification() : trx_map_(), mutex_() { }
        int append_trx(TrxHandlePtr);
        int test(wsdb_write_set*, bool);
        wsrep_seqno_t get_safe_to_discard_seqno() const;
        void purge_trxs_upto(wsrep_seqno_t);
        void set_trx_committed(TrxHandlePtr);
        TrxHandlePtr get_trx(wsrep_seqno_t);
        void deref_seqno(wsrep_seqno_t);
    private:
        TrxMap trx_map_;
        Mutex  mutex_;
    };
}

int galera::WsdbCertification::append_trx(TrxHandlePtr trx)
{
    Lock lock(mutex_);
    
    if (trx_map_.insert(make_pair(trx->get_global_seqno(), trx)).second == false)
    {
        gu_throw_fatal;
    }
    return wsdb_append_write_set(trx->get_write_set());
}

int galera::WsdbCertification::test(wsdb_write_set* write_set, bool bval)
{
    return wsdb_certification_test(write_set, bval);
}


wsrep_seqno_t galera::WsdbCertification::get_safe_to_discard_seqno() const
{
    return wsdb_get_safe_to_discard_seqno();
}

void galera::WsdbCertification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    Lock lock(mutex_); 
    wsdb_purge_trxs_upto(seqno);
    TrxMap::iterator lower_bound(trx_map_.lower_bound(seqno));
    trx_map_.erase(trx_map_.begin(), lower_bound);
}

void galera::WsdbCertification::set_trx_committed(TrxHandlePtr trx)
{
    if (trx->is_local() == true)
    {
        wsdb_set_local_trx_committed(trx->get_global_seqno());
    }
    else
    {
        wsdb_set_global_trx_committed(trx->get_global_seqno());
    }
}

galera::TrxHandlePtr galera::WsdbCertification::get_trx(wsrep_seqno_t seqno)
{
    Lock lock(mutex_);
    TrxMap::iterator i(trx_map_.find(seqno));
    if (i == trx_map_.end())
    {
        return TrxHandlePtr();
    }
    return i->second;
}

void galera::WsdbCertification::deref_seqno(wsrep_seqno_t seqno)
{
    wsdb_deref_seqno(seqno);
}





galera::Certification* galera::Certification::create(const std::string& conf)
{
    return new WsdbCertification();
}
