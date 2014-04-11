//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "key_data.hpp"

#if defined(GALERA_MULTIMASTER)
#include "replicator_smm.hpp"
#define REPL_CLASS galera::ReplicatorSMM
#else
#error "Not implemented"
#endif

#include "wsrep_params.hpp"

#include <cassert>


using galera::KeyOS;
using galera::WriteSet;
using galera::TrxHandle;
using galera::TrxHandleLock;


extern "C"
wsrep_status_t galera_init(wsrep_t* gh, const struct wsrep_init_args* args)
{
    assert(gh != 0);

    try
    {
        gh->ctx = new REPL_CLASS (args);
        wsrep_set_params(*reinterpret_cast<REPL_CLASS*>(gh->ctx),
                         args->options);
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
    }
    catch (std::exception& e)
    {
        log_error << e.what();
    }
    catch (gu::NotFound& e)
    {
        /* Unrecognized parameter (logged by gu::Config::set()) */
    }
#ifdef NDEBUG
    catch (...)
    {
        log_fatal << "non-standard exception";
    }
#endif

    return WSREP_NODE_FAIL;
}


extern "C"
uint64_t galera_capabilities(wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    static uint64_t const v4_caps(WSREP_CAP_MULTI_MASTER         |
                                  WSREP_CAP_CERTIFICATION        |
                                  WSREP_CAP_PARALLEL_APPLYING    |
                                  WSREP_CAP_TRX_REPLAY           |
                                  WSREP_CAP_ISOLATION            |
                                  WSREP_CAP_PAUSE                |
                                  WSREP_CAP_CAUSAL_READS);

    static uint64_t const v5_caps(WSREP_CAP_INCREMENTAL_WRITESET |
                                  WSREP_CAP_UNORDERED            |
                                  WSREP_CAP_PREORDERED);

    uint64_t caps(v4_caps);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    if (repl->repl_proto_ver() >= 5) caps |= v5_caps;

    return caps;
}


extern "C"
void galera_tear_down(wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    if (repl != 0)
    {
        delete repl;
        gh->ctx = 0;
    }
}


extern "C"
wsrep_status_t galera_parameters_set (wsrep_t* gh, const char* params)
{
    assert(gh != 0); // cppcheck-suppress nullPointer
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    // cppcheck-suppress nullPointer
    if (gh)
    {
        try
        {
            wsrep_set_params (*repl, params);
            return WSREP_OK;
        }
        catch (gu::NotFound&)
        {
            log_warn << "Unrecognized parameter in '" << params << "'";
            return WSREP_WARNING;
        }
        catch (std::exception& e)
        {
            log_debug << e.what(); // better logged in wsrep_set_params
        }
    }
    else
    {
        log_error << "Attempt to set parameter(s) on uninitialized replicator.";
    }

    return WSREP_NODE_FAIL;
}


extern "C"
char* galera_parameters_get (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    try
    {
        REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
        return wsrep_get_params(*repl);
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        return 0;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return 0;
    }
}


extern "C"
wsrep_status_t galera_connect (wsrep_t*     gh,
                               const char*  cluster_name,
                               const char*  cluster_url,
                               const char*  state_donor,
                               wsrep_bool_t bootstrap)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->connect(cluster_name, cluster_url,
                             state_donor ? state_donor : "", bootstrap);
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }
}


extern "C"
wsrep_status_t galera_disconnect(wsrep_t *gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->close();
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }
}


extern "C"
wsrep_status_t galera_recv(wsrep_t *gh, void *recv_ctx)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

#ifdef NDEBUG
    try
    {
#endif /* NDEBUG */

        return repl->async_recv(recv_ctx);

#ifdef NDEBUG
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();

        switch (e.get_errno())
        {
        case ENOTRECOVERABLE:
            return WSREP_FATAL;
        default:
            return WSREP_NODE_FAIL;
        }
    }
    catch (std::exception& e)
    {
        log_error << e.what();
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
    }
#endif // NDEBUG

    return WSREP_FATAL;
}

static TrxHandle*
get_local_trx(REPL_CLASS* const        repl,
              wsrep_ws_handle_t* const handle,
              bool const               create)
{
    TrxHandle* trx;
    assert(handle != 0);

    if (handle->opaque != 0)
    {
        trx = static_cast<TrxHandle*>(handle->opaque);
        assert(trx->trx_id() == handle->trx_id ||
               wsrep_trx_id_t(-1) == handle->trx_id);
        trx->ref();
    }
    else
    {
        trx = repl->get_local_trx(handle->trx_id, create);
        handle->opaque = trx;
    }

    return trx;
}

extern "C"
wsrep_status_t galera_replay_trx(wsrep_t*            gh,
                                 wsrep_ws_handle_t*  trx_handle,
                                 void*               recv_ctx)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(get_local_trx(repl, trx_handle, false));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->replay_trx(trx, recv_ctx);
    }
    catch (std::exception& e)
    {
        log_warn << "failed to replay trx: " << *trx;
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    repl->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t galera_abort_pre_commit(wsrep_t*       gh,
                                       wsrep_seqno_t  bf_seqno,
                                       wsrep_trx_id_t victim_trx)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS *   repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    wsrep_status_t retval;
    TrxHandle*     trx(repl->get_local_trx(victim_trx));

    if (!trx) return WSREP_OK;

    try
    {
        TrxHandleLock lock(*trx);
        repl->abort_trx(trx);
        retval = WSREP_OK;
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    repl->unref_local_trx(trx);

    return retval;
}

static inline void
discard_local_trx(REPL_CLASS*        repl,
                  wsrep_ws_handle_t* ws_handle,
                  TrxHandle*         trx)
{
    repl->unref_local_trx(trx);
    repl->discard_local_trx(trx);
    ws_handle->opaque = 0;
}

extern "C"
wsrep_status_t galera_post_commit (wsrep_t*            gh,
                                   wsrep_ws_handle_t*  ws_handle)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(get_local_trx(repl, ws_handle, false));

    if (trx == 0)
    {
        log_debug << "trx " << ws_handle->trx_id << " not found";
        return WSREP_OK;
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->post_commit(trx);
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    discard_local_trx(repl, ws_handle, trx);

    return retval;
}


extern "C"
wsrep_status_t galera_post_rollback(wsrep_t*            gh,
                                    wsrep_ws_handle_t*  ws_handle)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(get_local_trx(repl, ws_handle, false));

    if (trx == 0)
    {
        log_debug << "trx " << ws_handle->trx_id << " not found";
        return WSREP_OK;
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->post_rollback(trx);
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    discard_local_trx(repl, ws_handle, trx);

    return retval;
}


static inline void
append_data_array (TrxHandle*              const trx,
                   const struct wsrep_buf* const data,
                   size_t                  const count,
                   wsrep_data_type_t       const type,
                   bool                    const copy)
{
    for (size_t i(0); i < count; ++i)
    {
        trx->append_data(data[i].ptr, data[i].len, type, copy);
    }
}


extern "C"
wsrep_status_t galera_pre_commit(wsrep_t*           const gh,
                                 wsrep_conn_id_t    const conn_id,
                                 wsrep_ws_handle_t* const trx_handle,
                                 uint32_t           const flags,
                                 wsrep_trx_meta_t*  const meta)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    if (meta != 0)
    {
        meta->gtid = WSREP_GTID_UNDEFINED;
        meta->depends_on = WSREP_SEQNO_UNDEFINED;
    }

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandle* trx(get_local_trx(repl, trx_handle, /*rbr_data != 0*/ false));

    if (trx == 0)
    {
        // no data to replicate
        return WSREP_OK;
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        trx->set_conn_id(conn_id);
//        /* rbr_data should clearly persist over pre_commit() call */
//        append_data_array (trx, rbr_data, rbr_data_len, false, false);
        trx->set_flags(TrxHandle::wsrep_flags_to_trx_flags(flags));

        retval = repl->replicate(trx, meta);

        assert((!(retval == WSREP_OK || retval == WSREP_BF_ABORT) ||
                trx->global_seqno() > 0));

        if (retval == WSREP_OK)
        {
            assert(trx->last_seen_seqno() >= 0);
            retval = repl->pre_commit(trx, meta);
        }

        assert(retval == WSREP_OK || retval == WSREP_TRX_FAIL ||
               retval == WSREP_BF_ABORT);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();

        if (e.get_errno() == EMSGSIZE)
            retval = WSREP_SIZE_EXCEEDED;
        else
            retval = WSREP_NODE_FAIL;
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    repl->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t galera_append_key(wsrep_t*           const gh,
                                 wsrep_ws_handle_t* const trx_handle,
                                 const wsrep_key_t* const keys,
                                 size_t             const keys_num,
                                 wsrep_key_type_t   const key_type,
                                 wsrep_bool_t       const copy)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(get_local_trx(repl, trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        for (size_t i(0); i < keys_num; ++i)
        {
            galera::KeyData k (repl->trx_proto_ver(),
                               keys[i].key_parts,
                               keys[i].key_parts_num,
                               key_type,
                               copy);
            trx->append_key(k);
        }
        retval = WSREP_OK;
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }
    repl->unref_local_trx(trx);

    return retval;
}

extern "C"
wsrep_status_t galera_append_data(wsrep_t*                const wsrep,
                                  wsrep_ws_handle_t*      const trx_handle,
                                  const struct wsrep_buf* const data,
                                  size_t                  const count,
                                  wsrep_data_type_t       const type,
                                  wsrep_bool_t            const copy)
{
    assert(wsrep != 0);
    assert(wsrep->ctx != 0);
    assert(data != NULL);
    assert(count > 0);

    if (data == NULL)
    {
        // no data to replicate
        return WSREP_OK;
    }

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(wsrep->ctx));
    TrxHandle* trx(get_local_trx(repl, trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        if (WSREP_DATA_ORDERED == type)
            append_data_array(trx, data, count, type, copy);
        retval = WSREP_OK;
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    repl->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t galera_causal_read(wsrep_t*      const wsrep,
                                  wsrep_gtid_t* const gtid)
{
    assert(wsrep != 0);
    assert(wsrep->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(wsrep->ctx));
    wsrep_status_t retval;
    try
    {
        retval = repl->causal_read(gtid);
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }
    return retval;
}


extern "C"
wsrep_status_t galera_free_connection(wsrep_t*        const gh,
                                      wsrep_conn_id_t const conn_id)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        repl->discard_local_conn(conn_id);
        return WSREP_OK;
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        return WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }
}


extern "C"
wsrep_status_t galera_to_execute_start(wsrep_t*                const gh,
                                       wsrep_conn_id_t         const conn_id,
                                       const wsrep_key_t*      const keys,
                                       size_t                  const keys_num,
                                       const struct wsrep_buf* const data,
                                       size_t                  const count,
                                       wsrep_trx_meta_t*       const meta)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandle* trx(repl->local_conn_trx(conn_id, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        for (size_t i(0); i < keys_num; ++i)
        {
            galera::KeyData k(repl->trx_proto_ver(),
                              keys[i].key_parts,
                              keys[i].key_parts_num, WSREP_KEY_EXCLUSIVE,false);
            trx->append_key(k);
        }

        append_data_array(trx, data, count, WSREP_DATA_ORDERED, false);

        trx->set_flags(TrxHandle::wsrep_flags_to_trx_flags(
                           WSREP_FLAG_COMMIT |
                           WSREP_FLAG_ISOLATION));

        retval = repl->replicate(trx, meta);

        assert((retval == WSREP_OK && trx->global_seqno() > 0) ||
               (retval != WSREP_OK && trx->global_seqno() < 0));

        if (retval == WSREP_OK)
        {
            retval = repl->to_isolation_begin(trx, meta);
        }
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        retval = WSREP_FATAL;
    }

    if (retval != WSREP_OK) // galera_to_execute_end() won't be called
    {
        repl->discard_local_conn_trx(conn_id); // trx is not needed anymore

        if (trx->global_seqno() < 0) // no seqno -> no index -> no automatic purging
        {
            trx->unref(); // implicit destructor
        }
    }

    return retval;
}


extern "C"
wsrep_status_t galera_to_execute_end(wsrep_t*        const gh,
                                     wsrep_conn_id_t const conn_id)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    wsrep_status_t retval;
    TrxHandle* trx(repl->local_conn_trx(conn_id, false));

    try
    {
        TrxHandleLock lock(*trx);
        repl->to_isolation_end(trx);
        repl->discard_local_conn_trx(conn_id);
        return WSREP_OK;
        // trx will be unreferenced (destructed) during purge
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        return WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }

    return retval;
}


extern "C" wsrep_status_t
galera_preordered_collect (wsrep_t* const gh,
                           wsrep_po_handle_t*      const handle,
                           const struct wsrep_buf* const data,
                           size_t                  const count,
                           wsrep_bool_t            const copy)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    assert(handle != 0);
    assert(data != 0);
    assert(count > 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->preordered_collect(*handle, data, count, copy);
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        return WSREP_TRX_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }
}


extern "C" wsrep_status_t
galera_preordered_commit (wsrep_t* const gh,
                          wsrep_po_handle_t*      const handle,
                          const wsrep_uuid_t*     const source_id,
                          uint32_t                const flags,
                          int                     const pa_range,
                          wsrep_bool_t            const commit)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    assert(handle != 0);
    assert(source_id != 0 || false == commit);
    assert(pa_range  >= 0 || false == commit);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->preordered_commit(*handle, *source_id, flags, pa_range,
                                       commit);
    }
    catch (std::exception& e)
    {
        log_warn << e.what();
        return WSREP_TRX_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        return WSREP_FATAL;
    }
}


extern "C"
wsrep_status_t galera_sst_sent (wsrep_t*            const gh,
                                const wsrep_gtid_t* const state_id,
                                int                 const rcode)
{
    assert(gh       != 0);
    assert(gh->ctx  != 0);
    assert(state_id != 0);
    assert(rcode    <= 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    return repl->sst_sent(*state_id, rcode);
}


extern "C"
wsrep_status_t galera_sst_received (wsrep_t*            const gh,
                                    const wsrep_gtid_t* const state_id,
                                    const void*         const state,
                                    size_t              const state_len,
                                    int                 const rcode)
{
    assert(gh       != 0);
    assert(gh->ctx  != 0);
    assert(state_id != 0);
    assert(rcode    <= 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    if (rcode < 0) { assert(state_id->seqno == WSREP_SEQNO_UNDEFINED); }

    return repl->sst_received(*state_id, state, state_len, rcode);
}


extern "C"
wsrep_status_t galera_snapshot(wsrep_t*    wsrep,
                               const void* msg,
                               size_t      msg_len,
                               const char* donor_spec)
{
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
struct wsrep_stats_var* galera_stats_get (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS* repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    return const_cast<struct wsrep_stats_var*>(repl->stats_get());
}


extern "C"
void galera_stats_free (wsrep_t* gh, struct wsrep_stats_var* s)
{
    // REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    REPL_CLASS::stats_free(s);
}


extern "C"
void galera_stats_reset (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS* repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    repl->stats_reset();
}


extern "C"
wsrep_seqno_t galera_pause (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->pause();
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return -e.get_errno();
    }
}


extern "C"
wsrep_status_t galera_resume (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        repl->resume();
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
}


extern "C"
wsrep_status_t galera_desync (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        repl->desync();
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_TRX_FAIL;
    }
}


extern "C"
wsrep_status_t galera_resync (wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        repl->resync();
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
}


extern "C"
wsrep_status_t galera_lock (wsrep_t*     gh,
                            const char*  name,
                            wsrep_bool_t shared,
                            uint64_t     owner,
                            int64_t      timeout)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t galera_unlock (wsrep_t*    gh,
                              const char* name,
                              uint64_t    owner)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    return WSREP_OK;
}


extern "C"
bool galera_is_locked (wsrep_t*      gh,
                       const char*   name,
                       uint64_t*     owner,
                       wsrep_uuid_t* node)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    return false;
}


static wsrep_t galera_str = {
    WSREP_INTERFACE_VERSION,
    &galera_init,
    &galera_capabilities,
    &galera_parameters_set,
    &galera_parameters_get,
    &galera_connect,
    &galera_disconnect,
    &galera_recv,
    &galera_pre_commit,
    &galera_post_commit,
    &galera_post_rollback,
    &galera_replay_trx,
    &galera_abort_pre_commit,
    &galera_append_key,
    &galera_append_data,
    &galera_causal_read,
    &galera_free_connection,
    &galera_to_execute_start,
    &galera_to_execute_end,
    &galera_preordered_collect,
    &galera_preordered_commit,
    &galera_sst_sent,
    &galera_sst_received,
    &galera_snapshot,
    &galera_stats_get,
    &galera_stats_free,
    &galera_stats_reset,
    &galera_pause,
    &galera_resume,
    &galera_desync,
    &galera_resync,
    &galera_lock,
    &galera_unlock,
    &galera_is_locked,
    "Galera",
    GALERA_VER "(r" GALERA_REV ")",
    "Codership Oy <info@codership.com>",
    &galera_tear_down,
    NULL,
    NULL
};


/* Prototype to make compiler happy */
extern "C"
int wsrep_loader(wsrep_t *hptr);


extern "C"
int wsrep_loader(wsrep_t *hptr)
{
    if (!hptr)
        return EINVAL;

    try
    {
        *hptr = galera_str;
    }
    catch (...)
    {
        return ENOTRECOVERABLE;
    }

    return WSREP_OK;
}

