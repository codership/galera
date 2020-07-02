//
// Copyright (C) 2010-2017 Codership Oy <info@codership.com>
//

#ifndef GALERA_REPLICATOR_HPP
#define GALERA_REPLICATOR_HPP

#include "wsrep_api.h"
#include "galera_exception.hpp"
#include "trx_handle.hpp"

struct gcs_action;

#include <gu_config.hpp>
#include <string>

namespace galera
{
    class Statement;
    class RowId;

    //! @class Galera
    //
    // @brief Abstract Galera replicator interface

    class Replicator
    {
    public:

        struct Param
        {
            static std::string const debug_log;
#ifdef GU_DBUG_ON
            static std::string const dbug;
            static std::string const signal;
#endif // GU_DBUG_ON
        };

        static const char* const TRIVIAL_SST;
        static const char* const NO_SST;

        typedef enum
        {
            S_DESTROYED,
            S_CLOSED,
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

        virtual wsrep_cap_t capabilities() const = 0;
        virtual int trx_proto_ver() const = 0;
        virtual int repl_proto_ver() const = 0;

        virtual TrxHandleMasterPtr get_local_trx(wsrep_trx_id_t, bool) = 0;
        virtual void discard_local_trx(TrxHandleMaster* trx_id) = 0;

        virtual TrxHandleMasterPtr local_conn_trx(wsrep_conn_id_t conn_id,
                                                  bool            create) = 0;
        virtual void discard_local_conn_trx(wsrep_conn_id_t conn_id) = 0;

        virtual wsrep_status_t replicate(TrxHandleMaster&   trx,
                                         wsrep_trx_meta_t*  meta) = 0;
        virtual wsrep_status_t certify(TrxHandleMaster&     trx,
                                       wsrep_trx_meta_t*    meta) = 0;
        virtual wsrep_status_t replay_trx(TrxHandleMaster&  trx,
                                          TrxHandleLock&    lock,
                                          void*             replay_ctx) = 0;
        virtual wsrep_status_t abort_trx(TrxHandleMaster& trx,
                                         wsrep_seqno_t bf_seqno,
                                         wsrep_seqno_t* victim_seqno) = 0;
        virtual wsrep_status_t sync_wait(wsrep_gtid_t* upto,
                                         int           tout,
                                         wsrep_gtid_t* gtid) = 0;
        virtual wsrep_status_t last_committed_id(wsrep_gtid_t* gtid) const = 0;
        virtual wsrep_status_t to_isolation_begin(TrxHandleMaster&  trx,
                                                  wsrep_trx_meta_t* meta) = 0;
        virtual wsrep_status_t to_isolation_end(TrxHandleMaster&   trx,
                                                const wsrep_buf_t* err) = 0;
        virtual wsrep_status_t preordered_collect(wsrep_po_handle_t& handle,
                                                  const struct wsrep_buf* data,
                                                  size_t                  count,
                                                  bool                copy) = 0;
        virtual wsrep_status_t preordered_commit(wsrep_po_handle_t&  handle,
                                                 const wsrep_uuid_t& source,
                                                 uint64_t            flags,
                                                 int                 pa_range,
                                                 bool                commit) =0;
        virtual wsrep_status_t sst_sent(const wsrep_gtid_t& state_id,
                                        int                 rcode) = 0;
        virtual wsrep_status_t sst_received(const wsrep_gtid_t& state_id,
                                            const wsrep_buf_t*  state,
                                            int                 rcode) = 0;

        // action source interface
        virtual void process_trx(void* recv_ctx,
                                 const TrxHandleSlavePtr& trx) = 0;
        virtual void process_commit_cut(wsrep_seqno_t seq,
                                        wsrep_seqno_t seqno_l) = 0;
        virtual void process_conf_change(void*                    recv_ctx,
                                         const struct gcs_action& cc) = 0;
        virtual void process_state_req(void* recv_ctx, const void* req,
                                       size_t req_size,
                                       wsrep_seqno_t seqno_l,
                                       wsrep_seqno_t donor_seq) = 0;
        virtual void process_join(wsrep_seqno_t seqno, wsrep_seqno_t seqno_l) =0;
        virtual void process_sync(wsrep_seqno_t seqno_l) = 0;

        virtual void process_vote(wsrep_seqno_t seq,
                                  int64_t       code,
                                  wsrep_seqno_t seqno_l) = 0;

        virtual const struct wsrep_stats_var* stats_get() const = 0;
        virtual void                          stats_reset() = 0;
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

        virtual const wsrep_uuid_t& source_id() const = 0;

        virtual void cancel_seqnos(wsrep_seqno_t seqno_l,
                                   wsrep_seqno_t seqno_g) = 0;
        virtual bool corrupt() const = 0;

        static void register_params(gu::Config&);

    };
}

#endif // GALERA_REPLICATOR_HPP
