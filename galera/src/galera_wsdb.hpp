
#ifndef GALERA_BYPASS_WSDB_HPP
#define GALERA_GALERA_WSDB_HPP

#include "wsdb.hpp"
#include "wsrep_api.h"
#include <boost/unordered_map.hpp>

namespace galera
{
    
    class GaleraWsdb : public Wsdb
    {
        // TODO: This can be optimized
        class TrxHash
        {
        public:
            size_t operator()(const wsrep_trx_id_t& key) const
            {
                return (key & 0xffff);
            }
        };
        typedef boost::unordered_map<wsrep_trx_id_t, TrxHandle*, TrxHash> TrxMap;
        typedef boost::unordered_map<wsrep_conn_id_t, TrxHandle*> ConnQueryMap;

    public:
        TrxHandle* get_trx(wsrep_trx_id_t trx_id, bool create = false);
        TrxHandle* get_conn_query(wsrep_conn_id_t conn_id, 
                                    bool create = false);
        // Discard trx handle
        void discard_trx(wsrep_trx_id_t trx_id);
        void discard_conn(wsrep_conn_id_t conn_id);
        
        void append_query(TrxHandle*, const void* query, size_t query_len,
                          time_t, uint32_t);

        void append_row_key(TrxHandle*,
                            const void* dbtable, 
                            size_t dbtable_len,
                            const void* key, 
                            size_t key_len,
                            int action);
        
        void append_conn_query(TrxHandle*, const void* query,
                               size_t query_len);
        void discard_conn_query(wsrep_conn_id_t conn_id);

        void set_conn_variable(TrxHandle*, 
                               const void*, size_t,
                               const void*, size_t);
        void set_conn_database(TrxHandle*, const void*, size_t);
        void create_write_set(TrxHandle*, 
                              const void* rbr_data,
                              size_t rbr_data_len);
        std::ostream& operator<<(std::ostream& os) const;
        GaleraWsdb();
        ~GaleraWsdb();
        
    private:
        // Create new trx handle
        TrxHandle* create_trx(wsrep_trx_id_t trx_id);
        TrxHandle* create_conn_query(wsrep_conn_id_t conn_id);

        TrxMap       trx_map_;
        ConnQueryMap conn_query_map_;
        gu::Mutex    mutex_;
    };

}


#endif // GALERA_GALERA_WSDB_HPP
