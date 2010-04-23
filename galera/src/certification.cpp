
#include "certification.hpp"

#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <map>

using namespace std;
using namespace gu;

typedef map<wsrep_seqno_t, galera::TrxHandlePtr> TrxMap;

static TrxMap trx_map;
static Mutex mutex;

int galera::Certification::append_write_set(TrxHandlePtr trx)
{
    Lock lock(mutex);
    
    if (trx_map.insert(make_pair(trx->get_global_seqno(), trx)).second == false)
    {
        gu_throw_fatal;
    }
    return wsdb_append_write_set(trx->get_write_set());
}

int galera::Certification::test(wsdb_write_set* write_set, bool bval)
{
    return wsdb_certification_test(write_set, bval);
}


wsrep_seqno_t galera::Certification::get_safe_to_discard_seqno() const
{
    return wsdb_get_safe_to_discard_seqno();
}

void galera::Certification::purge_trxs_upto(wsrep_seqno_t seqno)
{
    Lock lock(mutex); 
    wsdb_purge_trxs_upto(seqno);
    TrxMap::iterator lower_bound(trx_map.lower_bound(seqno));
    trx_map.erase(trx_map.begin(), lower_bound);
}

void galera::Certification::set_trx_committed(wsrep_seqno_t seqno)
{
    wsdb_set_global_trx_committed(seqno);
}

galera::TrxHandlePtr galera::Certification::get_trx(wsrep_seqno_t seqno)
{
    Lock lock(mutex);
    TrxMap::iterator i(trx_map.find(seqno));
    if (i == trx_map.end())
    {
        return TrxHandlePtr();
    }
    return i->second;
}

void galera::Certification::deref_seqno(wsrep_seqno_t seqno)
{
    wsdb_deref_seqno(seqno);
}

galera::Certification* galera::Certification::create(const std::string& conf)
{
    return new Certification();
}
