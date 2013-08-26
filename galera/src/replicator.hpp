//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
//

#ifndef GALERA_REPLICATOR_HPP
#define GALERA_REPLICATOR_HPP

#include "wsrep_api.h"
#include "galera_exception.hpp"

#include <galerautils.hpp>
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

        static const char* const TRIVIAL_SST;

        typedef enum
        {
            S_DESTROYED,
            S_CLOSED,
            S_CLOSING,
            S_CONNECTED,
            S_JOINING,
            S_JOINED,
            S_SYNCED,
            S_DONOR
        } State;

        Replicator() { }
        virtual ~Replicator() { }
        virtual wsrep_status_t connect(const std::string& cluster_name,
                                       const std::string& cluster_url,
                                       const std::string& state_donor,
                                       bool               bootstrap) = 0;
        virtual wsrep_status_t close() = 0;
        virtual wsrep_status_t async_recv(void* recv_ctx) = 0;

        virtual int trx_proto_ver() const = 0;
        virtual TrxHandle* local_trx(wsrep_trx_id_t) = 0;
        virtual TrxHandle* local_trx(wsrep_ws_handle_t*, bool) = 0;
        virtual void unref_local_trx(TrxHandle* trx) = 0;
        virtual void discard_local_trx(wsrep_trx_id_t trx_id) = 0;

        virtual TrxHandle* local_conn_trx(wsrep_conn_id_t conn_id,
                                          bool create) = 0;
        virtual void discard_local_conn_trx(wsrep_conn_id_t conn_id) = 0;
        virtual void discard_local_conn(wsrep_conn_id_t conn_id) = 0;

        virtual wsrep_status_t replicate(TrxHandle* trx) = 0;
        virtual wsrep_status_t pre_commit(TrxHandle* trx, wsrep_trx_meta_t*) =0;
        virtual wsrep_status_t post_commit(TrxHandle* trx) = 0;
        virtual wsrep_status_t post_rollback(TrxHandle* trx) = 0;
        virtual wsrep_status_t replay_trx(TrxHandle* trx, void* replay_ctx) = 0;
        virtual void abort_trx(TrxHandle* trx) = 0;
        virtual wsrep_status_t causal_read(wsrep_gtid_t*) = 0;
        virtual wsrep_status_t to_isolation_begin(TrxHandle* trx,
                                                  wsrep_trx_meta_t*) = 0;
        virtual wsrep_status_t to_isolation_end(TrxHandle* trx) = 0;
        virtual wsrep_status_t handle_preordered(const wsrep_uuid_t& source,
                                                 uint64_t flags,
                                                 const struct wsrep_buf* data,
                                                 int count,
                                                 int pa_range,
                                                 bool copy) = 0;
        virtual wsrep_status_t sst_sent(const wsrep_uuid_t& uuid,
                                        wsrep_seqno_t seqno) = 0;
        virtual wsrep_status_t sst_received(const wsrep_uuid_t& uuid,
                                            wsrep_seqno_t       seqno,
                                            const void*         state,
                                            size_t              state_len) = 0;

        // action source interface
        virtual void process_trx(void* recv_ctx, TrxHandle* trx) = 0;
        virtual void process_commit_cut(wsrep_seqno_t seq,
                                        wsrep_seqno_t seqno_l) = 0;
        virtual void process_conf_change(void*                    recv_ctx,
                                         const wsrep_view_info_t& view_info,
                                         int                      repl_proto,
                                         State                    next_state,
                                         wsrep_seqno_t            seqno_l) = 0;
        virtual void process_state_req(void* recv_ctx, const void* req,
                                       size_t req_size,
                                       wsrep_seqno_t seqno_l,
                                       wsrep_seqno_t donor_seq) = 0;
        virtual void process_join(wsrep_seqno_t seqno, wsrep_seqno_t seqno_l) = 0;
        virtual void process_sync(wsrep_seqno_t seqno_l) = 0;

        virtual const struct wsrep_stats_var* stats_get()  const = 0;
        // static void stats_free(struct wsrep_stats_var*) must be declared in
        // the child class

        /*! @throws NotFound */
        virtual void        param_set (const std::string& key,
                                       const std::string& value) = 0;

        /*! @throws NotFound */
        virtual std::string param_get (const std::string& key) const = 0;

        virtual const gu::Config& params() const = 0;

        virtual wsrep_seqno_t pause()  = 0;
        virtual void          resume() = 0;

        virtual void          desync() = 0;
        virtual void          resync() = 0;
    };
}

#endif // GALERA_REPLICATOR_HPP
