
#include "wsdb.hpp"
#include "gu_lock.hpp"
#include "gu_throw.hpp"

#include <boost/unordered_map.hpp>

using namespace std;
using namespace gu;


typedef boost::unordered_map<wsrep_trx_id_t, galera::TrxHandlePtr> TrxMap;
static TrxMap trx_map;
static Mutex mutex;

galera::TrxHandlePtr galera::Wsdb::create_trx(wsrep_trx_id_t id)
{
    Lock lock(mutex);
    pair<TrxMap::iterator, bool> i = trx_map.insert(
        make_pair(id, TrxHandlePtr(new TrxHandle(id, true))));
    if (i.second == false)
        gu_throw_fatal;
    return i.first->second;
}


galera::TrxHandlePtr galera::Wsdb::get_trx(wsrep_trx_id_t id, bool create)
{
    Lock lock(mutex);
    
    TrxMap::iterator i;
    if ((i = trx_map.find(id)) == trx_map.end())
    {
        if (create == true) return create_trx(id);
        else return TrxHandlePtr();
    }
    return i->second;
}

void galera::Wsdb::discard_trx(wsrep_trx_id_t id)
{
    Lock lock(mutex);
    TrxMap::iterator i;
    if ((i = trx_map.find(id)) != trx_map.end())
    {
        trx_map.erase(i);
    }
}


galera::Wsdb* galera::Wsdb::create(const string& conf)
{
    return new Wsdb();
}
