
#ifndef GALERA_WSDB_HPP
#define GALERA_WSDB_HPP

#include "trx_handle.hpp"

namespace galera
{
    class Wsdb
    {
    public:
        // Get trx handle from wsdb
        virtual TrxHandlePtr get_trx(wsrep_trx_id_t id, bool create = false);
        
        // Discard trx handle
        virtual void discard_trx(wsrep_trx_id_t id);
        
        // Create wsdb instance
        static Wsdb* create(const std::string& conf);
        
        virtual ~Wsdb() { }
    private:
        // Create new trx handle
        virtual TrxHandlePtr create_trx(wsrep_trx_id_t id);
        Wsdb() { }
    };
}


#endif // GALERA_WSDB_HPP
