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

using galera::WriteSet;
using galera::TrxHandle;
using galera::TrxHandleLock;


extern "C"
wsrep_status_t galera_init(wsrep_t* gh, const struct wsrep_init_args* args)
{
    try
    {
        gh->ctx = new REPL_CLASS (args);
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
            WSREP_CAP_PAUSE);
}


extern "C"
void galera_tear_down(wsrep_t* gh)
{
    assert(gh != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
    REPL_CLASS *   repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    wsrep_status_t retval;
    TrxHandle* trx(repl->local_trx(victim_trx));

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
wsrep_status_t galera_abort_slave_trx (wsrep_t*      gh,
                                       wsrep_seqno_t bf_seqno,
                                       wsrep_seqno_t victim_seqno)
{
    log_warn << "Trx " << bf_seqno << " tries to abort";
    log_warn << "Trx " << victim_seqno;
    log_warn << "This call is bogus and should be removed from API. See #335";
    return WSREP_WARNING;
}


extern "C"
wsrep_status_t galera_post_commit (wsrep_t*            gh,
                                   wsrep_trx_handle_t* trx_handle)
{
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
                                 wsrep_seqno_t*      global_seqno)
{
    assert(gh != 0 && gh->ctx != 0);

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
        trx->set_flags(TrxHandle::F_COMMIT);

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
wsrep_status_t galera_append_row_key(wsrep_t*            gh,
                                     wsrep_trx_handle_t* trx_handle,
                                     const char*         dbtable,
                                     size_t              dbtable_len,
                                     const char*         key,
                                     size_t              key_len,
                                     enum wsrep_action   action)
{
    assert(gh != 0 && gh->ctx != 0);
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(repl->local_trx(trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        trx->append_row_id(dbtable, dbtable_len, key, key_len);
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
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t galera_set_variable(wsrep_t*              gh,
                                   const wsrep_conn_id_t conn_id,
                                   const char*           key,
                                   const size_t          key_len,
                                   const char*           query,
                                   const size_t          query_len)
{

    if (!strncmp(key, "wsrep_debug", key_len)) {
        char value[256] = {0,};
        gu_debug("GALERA set value: %s" , value);
        strncpy(value, query, query_len);
        const char *set_query= "wsrep_debug=ON";

        if (strstr(value, set_query)) {
            gu_info("GALERA enabling debug logging: %s" , value);
            gu_conf_debug_on();
        } else {
            gu_info("GALERA disabling debug logging: %s %s" , value, set_query);
            gu_conf_debug_off();
        }
        return WSREP_OK;
    }

    {
        char var[256] = {0,};
        strncpy(var, key, sizeof(var) - 1);
        gu_debug("GALERA set var: %s" , var);
    }

    return WSREP_OK;
}


extern "C"
wsrep_status_t galera_set_database(wsrep_t*              gh,
                                   const wsrep_conn_id_t conn_id,
                                   const char*           query,
                                   const size_t          query_len)
{
    assert(gh != 0 && gh->ctx != 0);
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    try
    {
        if (query == 0) repl->discard_local_conn(conn_id);
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
wsrep_status_t galera_to_execute_start(wsrep_t*        gh,
                                       wsrep_conn_id_t conn_id,
                                       const void*     query,
                                       size_t          query_len,
                                       wsrep_seqno_t*  global_seqno)
{
    assert(gh != 0 && gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandle* trx(repl->local_conn_trx(conn_id, true));
    assert(trx != 0);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        trx->append_data(query, query_len);
        trx->set_flags(TrxHandle::F_COMMIT);

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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
    assert(gh != 0 && gh->ctx != 0);
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
void galera_resume (wsrep_t* gh)
{
    assert(gh != 0 && gh->ctx != 0);
    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    repl->resume();
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
    &galera_abort_slave_trx,
    &galera_append_query,
    &galera_append_row_key,
    &galera_append_data,
    &galera_causal_read,
    &galera_set_variable,
    &galera_set_database,
    &galera_to_execute_start,
    &galera_to_execute_end,
    &galera_sst_sent,
    &galera_sst_received,
    &galera_snapshot,
    &galera_stats_get,
    &galera_stats_free,
    &galera_pause,
    &galera_resume,
    "Galera",
    "0.8pre",
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

