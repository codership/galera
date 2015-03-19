//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//
#ifndef GALERA_WSDB_HPP
#define GALERA_WSDB_HPP

#include "trx_handle.hpp"
#include "wsrep_api.h"
#include "gu_unordered.hpp"

namespace galera
{
    class Wsdb
    {

        class Conn
        {
        public:
            Conn(wsrep_conn_id_t conn_id)
                :
                conn_id_(conn_id),
                trx_(0)
            { }

            Conn(const Conn& other)
                :
                conn_id_(other.conn_id_),
                trx_(other.trx_)
            { }

            ~Conn() { if (trx_ != 0) trx_->unref(); }

            void assign_trx(TrxHandleMaster* trx)
            {
                if (trx_ != 0) trx_->unref();
                trx_ = trx;
            }

            TrxHandleMaster* get_trx()
            {
                return trx_;
            }

        private:
            void operator=(const Conn&);
            wsrep_conn_id_t conn_id_;
            TrxHandleMaster* trx_;
        };


        class TrxHash
        {
        public:
            size_t operator()(const wsrep_trx_id_t& key) const { return key; }
        };

        typedef gu::UnorderedMap<wsrep_trx_id_t, TrxHandleMaster*, TrxHash>
        TrxMap;

        class ConnHash
        {
        public:
            size_t operator()(const wsrep_conn_id_t& key) const { return key; }
        };

        typedef gu::UnorderedMap<wsrep_conn_id_t, Conn, ConnHash> ConnMap;

    public:

        TrxHandleMaster* get_trx(const TrxHandleMaster::Params& params,
                                 const wsrep_uuid_t&            source_id,
                                 wsrep_trx_id_t                 trx_id,
                                 bool                           create = false);

        void discard_trx(wsrep_trx_id_t trx_id);

        TrxHandleMaster* get_conn_query(const TrxHandleMaster::Params&,
                                        const wsrep_uuid_t&,
                                        wsrep_conn_id_t conn_id,
                                        bool create = false);

        void discard_conn(wsrep_conn_id_t conn_id);
        void discard_conn_query(wsrep_conn_id_t conn_id);

        Wsdb();
        ~Wsdb();

        void print(std::ostream& os) const;

    private:
        // Find existing trx handle in the map
        TrxHandleMaster* find_trx(wsrep_trx_id_t trx_id);

        // Create new trx handle
        TrxHandleMaster* create_trx(const TrxHandleMaster::Params& params,
                                    const wsrep_uuid_t&            source_id,
                                    wsrep_trx_id_t                 trx_id);

        Conn*      get_conn(wsrep_conn_id_t conn_id, bool create);

        static const size_t trx_mem_limit_ = 1 << 20;

        TrxHandleMaster::Pool trx_pool_;

        TrxMap       trx_map_;
        gu::Mutex    trx_mutex_;
        ConnMap      conn_map_;
        gu::Mutex    conn_mutex_;
    };

    inline std::ostream& operator<<(std::ostream& os, const Wsdb& w)
    {
        w.print(os); return os;
    }
}


#endif // GALERA_WSDB_HPP
