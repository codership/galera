//
// Copyright (C) 2010 Codership Oy <info@codership.com>
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
                                       const std::string& state_donor) = 0;
        virtual wsrep_status_t close() = 0;
        virtual wsrep_status_t async_recv(void* recv_ctx) = 0;

        virtual TrxHandle* local_trx(wsrep_trx_id_t) = 0;
        virtual TrxHandle* local_trx(wsrep_trx_handle_t*, bool) = 0;
        virtual void unref_local_trx(TrxHandle* trx) = 0;
        virtual void discard_local_trx(wsrep_trx_id_t trx_id) = 0;

        virtual TrxHandle* local_conn_trx(wsrep_conn_id_t conn_id,
                                          bool create) = 0;
        virtual void discard_local_conn_trx(wsrep_conn_id_t conn_id) = 0;
        virtual void discard_local_conn(wsrep_conn_id_t conn_id) = 0;

        virtual wsrep_status_t replicate(TrxHandle* trx) = 0;
        virtual wsrep_status_t pre_commit(TrxHandle* trx) = 0;
        virtual wsrep_status_t post_commit(TrxHandle* trx) = 0;
        virtual wsrep_status_t post_rollback(TrxHandle* trx) = 0;
        virtual wsrep_status_t replay_trx(TrxHandle* trx, void* replay_ctx) = 0;
        virtual void abort_trx(TrxHandle* trx) throw (gu::Exception) = 0;
        virtual wsrep_status_t causal_read(wsrep_seqno_t*) = 0;
        virtual wsrep_status_t to_isolation_begin(TrxHandle* trx) = 0;
        virtual wsrep_status_t to_isolation_end(TrxHandle* trx) = 0;
        virtual wsrep_status_t sst_sent(const wsrep_uuid_t& uuid,
                                        wsrep_seqno_t seqno) = 0;
        virtual wsrep_status_t sst_received(const wsrep_uuid_t& uuid,
                                            wsrep_seqno_t       seqno,
                                            const void*         state,
                                            size_t              state_len) = 0;

        // action source interface
        virtual void process_trx(void* recv_ctx, TrxHandle* trx)
            throw (ApplyException) = 0;
        virtual void process_commit_cut(wsrep_seqno_t seq,
                                        wsrep_seqno_t seqno_l)
            throw (gu::Exception) = 0;
        virtual void process_view_info(void* recv_ctx,
                                       const wsrep_view_info_t& view_info,
                                       State next_state,
                                       wsrep_seqno_t seqno_l)
            throw (gu::Exception) = 0;
        virtual void process_state_req(void* recv_ctx, const void* req,
                                       size_t req_size,
                                       wsrep_seqno_t seqno_l,
                                       wsrep_seqno_t donor_seq)
            throw (gu::Exception) = 0;
        virtual void process_join(wsrep_seqno_t seqno, wsrep_seqno_t seqno_l)
            throw (gu::Exception) = 0;
        virtual void process_sync(wsrep_seqno_t seqno_l) = 0;

        // wsrep_status_t snapshot();
        virtual const struct wsrep_stats_var* stats() const = 0;

        virtual void        param_set (const std::string& key,
                                       const std::string& value)
            throw (gu::Exception, gu::NotFound) = 0;

        virtual std::string param_get (const std::string& key) const
            throw (gu::Exception, gu::NotFound) = 0;

        virtual const gu::Config& params() const = 0;

        virtual wsrep_seqno_t pause()  throw (gu::Exception) = 0;
        virtual void          resume() throw () = 0;

        virtual void          desync() throw (gu::Exception) = 0;
        virtual void          resync() throw (gu::Exception) = 0;
    };
}

#endif // GALERA_REPLICATOR_HPP
