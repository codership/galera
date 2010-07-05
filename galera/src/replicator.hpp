//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_REPLICATOR_HPP
#define GALERA_REPLICATOR_HPP

#include "wsrep_api.h"

#include <string>

namespace galera
{
    class Statement;
    class RowId;
    class TrxHandle;

    //! @class Galera
    //
    // @brief Abstract Galera replicator interface

    class Replicator
    {
    public:
        Replicator() { }
        virtual ~Replicator() { }
        virtual wsrep_status_t connect(const std::string& cluster_name,
                                       const std::string& cluster_url,
                                       const std::string& state_donor) = 0;
        virtual wsrep_status_t close() = 0;
        virtual wsrep_status_t async_recv(void* recv_ctx) = 0;

        virtual TrxHandle* local_trx(wsrep_trx_id_t) = 0;
        virtual TrxHandle* local_trx(wsrep_trx_handle_t*, bool) = 0;
        virtual void unref_local_trx(TrxHandle* trx) = 0;
        virtual void discard_local_trx(wsrep_trx_id_t trx_id) = 0;

        virtual TrxHandle* local_conn_trx(wsrep_conn_id_t conn_id,
                                          bool create) = 0;
        virtual void set_default_context(wsrep_conn_id_t conn_id,
                                         const void* cxt, size_t cxt_len) = 0;
        virtual void discard_local_conn(wsrep_conn_id_t conn_id) = 0;
        virtual wsrep_status_t replicate(TrxHandle* trx) = 0;
        virtual wsrep_status_t pre_commit(TrxHandle* trx) = 0;
        virtual wsrep_status_t post_commit(TrxHandle* trx) = 0;
        virtual wsrep_status_t post_rollback(TrxHandle* trx) = 0;
        virtual wsrep_status_t replay(TrxHandle* trx, void* replay_ctx) = 0;
        virtual wsrep_status_t abort(TrxHandle* trx) = 0;
        virtual wsrep_status_t causal_read(wsrep_seqno_t*) const = 0;
        virtual wsrep_status_t to_isolation_begin(TrxHandle* trx) = 0;
        virtual wsrep_status_t to_isolation_end(TrxHandle* trx) = 0;
        virtual wsrep_status_t sst_sent(const wsrep_uuid_t& uuid,
                                        wsrep_seqno_t seqno) = 0;
        virtual wsrep_status_t sst_received(const wsrep_uuid_t& uuid,
                                            wsrep_seqno_t       seqno,
                                            const void*         state,
                                            size_t              state_len) = 0;
        // wsrep_status_t snapshot();
        virtual const struct wsrep_status_var* status() const = 0;
    };
}

#endif // GALERA_REPLICATOR_HPP
