//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#ifndef GALERA_WSDB_HPP
#define GALERA_WSDB_HPP

#include "trx_handle.hpp"

namespace galera
{

    

    class Wsdb
    {
    public:
        // Get trx handle from wsdb
        virtual TrxHandlePtr get_trx(wsrep_trx_id_t trx_id, 
                                     bool create = false) = 0;

        virtual TrxHandlePtr get_conn_query(wsrep_conn_id_t conn_id, 
                                            bool create = false) = 0;

        // Discard trx handle
        virtual void discard_trx(wsrep_trx_id_t trx_id) = 0;
        virtual void discard_conn(wsrep_conn_id_t conn_id) = 0;

        virtual void discard_conn_query(wsrep_conn_id_t conn_id) = 0;

        // Append query
        virtual void append_query(TrxHandlePtr&,
                                  const void* query,
                                  size_t query_len,
                                  time_t time,
                                  uint32_t rnd) = 0;



        // Append row key
        virtual void append_row_key(TrxHandlePtr&,
                                    const void* dbtable, 
                                    size_t dbtable_len,
                                    const void* key, 
                                    size_t key_len,
                                    int action) = 0;

        virtual void append_conn_query(TrxHandlePtr&, const void* query,
                                       size_t query_len) = 0;

        virtual void set_conn_variable(TrxHandlePtr&, const void*, size_t,
                                       const void*, size_t) = 0;
        virtual void set_conn_database(TrxHandlePtr&, const void*, size_t) = 0;

        // Create write set 
        virtual void create_write_set(TrxHandlePtr&, 
                                      const void* rbr_data = 0,
                                      size_t rbr_data_len = 0) = 0;


        virtual std::ostream& operator<<(std::ostream&) const = 0;

        // Create wsdb instance
        static Wsdb* create(const std::string& conf);

        virtual ~Wsdb() { }

    protected:
        Wsdb() { }
    };

    inline std::ostream& operator<<(std::ostream& os, const Wsdb& wsdb)
    {
        return wsdb.operator<<(os);
    }

}


#endif // GALERA_WSDB_HPP
