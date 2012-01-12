//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#if defined(GALERA_MULTIMASTER)
#include "replicator_smm.hpp"
#define REPL_CLASS galera::ReplicatorSMM
#else
#error "Not implemented"
#endif

#include "wsrep_params.hpp"

#include <cassert>

using galera::Key;
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
    catch (...)
    {
        log_fatal << "non-standard exception";
    }

    return WSREP_NODE_FAIL;
}


extern "C"
uint64_t galera_capabilities(wsrep_t* gh)
{
    return (WSREP_CAP_MULTI_MASTER      |
            WSREP_CAP_CERTIFICATION     |
            WSREP_CAP_PARALLEL_APPLYING |
            WSREP_CAP_TRX_REPLAY        |
            WSREP_CAP_ISOLATION         |
            WSREP_CAP_PAUSE             |
            WSREP_CAP_CAUSAL_READS);
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
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    if (gh)
    {
        try
        {
            wsrep_set_params (*repl, params);
            return WSREP_OK;
        }
        catch (std::exception& e)
        {
            log_error << e.what();
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
wsrep_status_t galera_connect (wsrep_t*    gh,
                               const char* cluster_name,
                               const char* cluster_url,
                               const char* state_donor)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        return repl->connect(cluster_name, cluster_url, state_donor);
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

    try
    {
        return repl->async_recv(recv_ctx);
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

    return WSREP_FATAL;
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
    TrxHandle*     trx(repl->local_trx(victim_trx));

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


extern "C"
wsrep_status_t galera_post_commit (wsrep_t*            gh,
                                   wsrep_trx_handle_t* trx_handle)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(repl->local_trx(trx_handle, false));

    if (trx == 0)
    {
        log_debug << "trx " << trx_handle->trx_id << " not found";
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

    repl->unref_local_trx(trx);
    repl->discard_local_trx(trx->trx_id());
    trx_handle->opaque = 0;

    return retval;
}


extern "C"
wsrep_status_t galera_post_rollback(wsrep_t*            gh,
                                    wsrep_trx_handle_t* trx_handle)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(repl->local_trx(trx_handle, false));

    if (trx == 0)
    {
        log_debug << "trx " << trx_handle->trx_id << " not found";
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

    repl->unref_local_trx(trx);
    repl->discard_local_trx(trx->trx_id());
    trx_handle->opaque = 0;

    return retval;
}


extern "C"
wsrep_status_t galera_pre_commit(wsrep_t*            gh,
                                 wsrep_conn_id_t     conn_id,
                                 wsrep_trx_handle_t* trx_handle,
                                 const void*         rbr_data,
                                 size_t              rbr_data_len,
                                 uint64_t            flags __attribute__((unused)) ,
                                 wsrep_seqno_t*      global_seqno)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    *global_seqno = WSREP_SEQNO_UNDEFINED;

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandle* trx(repl->local_trx(trx_handle, rbr_data != 0));

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
        trx->append_data(rbr_data, rbr_data_len);
        trx->set_flags(
            TrxHandle::F_COMMIT |
            ((flags & WSREP_FLAG_PA_SAFE) ? 0 : TrxHandle::F_PA_UNSAFE)
            );

        retval = repl->replicate(trx);

        assert(((retval == WSREP_OK || retval == WSREP_BF_ABORT) &&
                trx->global_seqno() > 0) ||
               (retval != WSREP_OK && trx->global_seqno() < 0));

        if (retval == WSREP_OK)
        {
            *global_seqno = trx->global_seqno();
            retval = repl->pre_commit(trx);
        }
        assert(retval == WSREP_OK || retval == WSREP_TRX_FAIL ||
               retval == WSREP_BF_ABORT);
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
wsrep_status_t galera_append_query(wsrep_t*            gh,
                                   wsrep_trx_handle_t* trx_handle,
                                   const char*         query,
                                   const time_t        timeval,
                                   const uint32_t      randseed)
{
    log_warn << "galera_append_query() is deprecated";
    return WSREP_CONN_FAIL;
}


extern "C"
wsrep_status_t galera_append_key(wsrep_t*            gh,
                                 wsrep_trx_handle_t* trx_handle,
                                 const wsrep_key_t*  key,
                                 size_t              key_len,
                                 enum wsrep_action   action)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(repl->local_trx(trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        for (size_t i(0); i < key_len; ++i)
        {
            trx->append_key(galera::Key(repl->protocol_version(),
                                        key[i].key_parts,
                                        key[i].key_parts_len));
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
wsrep_status_t galera_append_data(wsrep_t*            wsrep,
                                  wsrep_trx_handle_t* trx_handle,
                                  const void*         data,
                                  size_t              data_len)
{
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t galera_causal_read(wsrep_t*       wsrep,
                                  wsrep_seqno_t* seqno)
{
    assert(wsrep != 0);
    assert(wsrep->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(wsrep->ctx));
    wsrep_status_t retval;
    try
    {
        retval = repl->causal_read(seqno);
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
wsrep_status_t galera_free_connection(wsrep_t*              gh,
                                      const wsrep_conn_id_t conn_id)
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
wsrep_status_t galera_to_execute_start(wsrep_t*           gh,
                                       wsrep_conn_id_t    conn_id,
                                       const wsrep_key_t* key,
                                       size_t             key_len,
                                       const void*        query,
                                       size_t             query_len,
                                       wsrep_seqno_t*     global_seqno)
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
        for (size_t i(0); i < key_len; ++i)
        {
            trx->append_key(Key(repl->protocol_version(),
                                key[i].key_parts,
                                key[i].key_parts_len));
        }
        trx->append_data(query, query_len);
        trx->set_flags(TrxHandle::F_COMMIT | TrxHandle::F_ISOLATION);

        retval = repl->replicate(trx);

        assert((retval == WSREP_OK && trx->global_seqno() > 0) ||
               (retval != WSREP_OK && trx->global_seqno() < 0));

        *global_seqno = trx->global_seqno();

        if (retval == WSREP_OK)
        {
            retval = repl->to_isolation_begin(trx);
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

        if (*global_seqno < 0) // no seqno -> no index -> no automatic purging
        {
            trx->unref(); // implicit destructor
        }
    }

    return retval;
}


extern "C"
wsrep_status_t galera_to_execute_end(wsrep_t* gh, wsrep_conn_id_t conn_id)
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


extern "C"
wsrep_status_t galera_replay_trx(wsrep_t*            gh,
                                 wsrep_trx_handle_t* trx_handle,
                                 void*               recv_ctx)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(repl->local_trx(trx_handle, false));
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
wsrep_status_t galera_sst_sent (wsrep_t*            gh,
                                const wsrep_uuid_t* uuid,
                                wsrep_seqno_t       seqno)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    return repl->sst_sent(*uuid, seqno);
}


extern "C"
wsrep_status_t galera_sst_received (wsrep_t*            gh,
                                    const wsrep_uuid_t* uuid,
                                    wsrep_seqno_t       seqno,
                                    const char*         state,
                                    size_t              state_len)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    return repl->sst_received(*uuid, seqno, state, state_len);
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
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    return const_cast<struct wsrep_stats_var*>(repl->stats());
}


extern "C"
void galera_stats_free (wsrep_t* gh, struct wsrep_stats_var* s)
{
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
wsrep_status_t galera_lock (wsrep_t* gh,
                            const char* name,
                            int64_t     owner,
                            int64_t     timeout)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t galera_unlock (wsrep_t* gh,
                              const char* name,
                              int64_t     owner)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    return WSREP_OK;
}


extern "C"
bool galera_is_locked (wsrep_t* gh,
                       const char*   name,
                       int64_t*      owner,
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
    &galera_append_query,
    &galera_append_key,
    &galera_append_data,
    &galera_causal_read,
    &galera_free_connection,
    &galera_to_execute_start,
    &galera_to_execute_end,
    &galera_sst_sent,
    &galera_sst_received,
    &galera_snapshot,
    &galera_stats_get,
    &galera_stats_free,
    &galera_pause,
    &galera_resume,
    &galera_desync,
    &galera_resync,
    &galera_lock,
    &galera_unlock,
    &galera_is_locked,
    "Galera",
    GALERA_VER"(r"GALERA_REV")",
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

