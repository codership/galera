//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "mm_provider.hpp"

#include <cassert>

using galera::MM;
using galera::WriteSet;
using galera::TrxHandle;
using galera::TrxHandleLock;

extern "C"
wsrep_status_t mm_galera_init(wsrep_t* gh,
                              const struct wsrep_init_args* args)
{
    try
    {
        gh->ctx = new MM(args);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
    return WSREP_OK;
}


extern "C"
void mm_galera_tear_down(wsrep_t *gh)
{
    assert(gh != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    if (mm != 0)
    {
        delete mm;
        gh->ctx = 0;
    }
}

extern "C"
wsrep_status_t mm_galera_options_set (wsrep_t* gh, const char* opts_str)
{
    // return galera_options_from_string (&galera_opts, opts_str);
    return WSREP_OK;
}


extern "C"
char* mm_galera_options_get (wsrep_t* gh)
{
    // return galera_options_to_string (&galera_opts);
    return 0;
}


extern "C"
wsrep_status_t mm_galera_connect (wsrep_t *gh,
                                  const char* cluster_name,
                                  const char* cluster_url,
                                  const char* state_donor)
{
    assert(gh != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));

    try
    {
        return mm->connect(cluster_name, cluster_url, state_donor);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        return WSREP_FATAL;
    }
}

extern "C"
wsrep_status_t mm_galera_disconnect(wsrep_t *gh)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    try
    {
        return mm->close();
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        return WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        return WSREP_FATAL;
    }
}



extern "C"
wsrep_status_t mm_galera_recv(wsrep_t *gh, void *recv_ctx)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    try
    {
        return mm->async_recv(recv_ctx);
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
    catch (...)
    {
        log_fatal << "uncaught exception";
        return WSREP_FATAL;
    }
}

extern "C"
wsrep_status_t mm_galera_abort_pre_commit(wsrep_t *gh,
                                          wsrep_seqno_t bf_seqno,
                                          wsrep_trx_id_t victim_trx)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    wsrep_status_t retval;
    TrxHandle* trx(mm->local_trx(victim_trx));
    if (trx == 0)
    {
        return WSREP_OK;
    }
    try
    {
        TrxHandleLock lock(*trx);
        retval = mm->abort(trx);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }
    mm->unref_local_trx(trx);

    return retval;
}

extern "C"
wsrep_status_t mm_galera_abort_slave_trx(
    wsrep_t *gh, wsrep_seqno_t bf_seqno, wsrep_seqno_t victim_seqno
    )
{
    log_warn << "Trx " << bf_seqno << " tries to abort";
    log_warn << "Trx " << victim_seqno;
    log_warn << "This call is bogus and should be removed from API. See #335";
    return WSREP_WARNING;
}


extern "C"
wsrep_status_t mm_galera_post_commit(wsrep_t *gh,
                                     wsrep_trx_handle_t* trx_handle)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    TrxHandle* trx(mm->local_trx(trx_handle, false));
    if (trx == 0)
    {
        log_debug << "trx " << trx_handle->trx_id << " not found";
        return WSREP_OK;
    }

    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        retval = mm->post_commit(trx);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }

    mm->unref_local_trx(trx);
    mm->discard_local_trx(trx->trx_id());
    trx_handle->opaque = 0;

    return retval;
}

extern "C"
wsrep_status_t mm_galera_post_rollback(wsrep_t *gh,
                                       wsrep_trx_handle_t* trx_handle)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    TrxHandle* trx(mm->local_trx(trx_handle, false));
    if (trx == 0)
    {
        log_debug << "trx " << trx_handle->trx_id << " not found";
        return WSREP_OK;
    }

    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        retval = mm->post_rollback(trx);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }

    mm->unref_local_trx(trx);
    mm->discard_local_trx(trx->trx_id());
    trx_handle->opaque = 0;

    return retval;
}


extern "C"
wsrep_status_t mm_galera_pre_commit(wsrep_t *gh,
                                    wsrep_conn_id_t conn_id,
                                    wsrep_trx_handle_t* trx_handle,
                                    const void *rbr_data,
                                    size_t rbr_data_len,
                                    wsrep_seqno_t* global_seqno)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));

    TrxHandle* trx(mm->local_trx(trx_handle, rbr_data != 0));
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

        retval = mm->replicate(trx);
        if (retval == WSREP_OK)
        {
            retval = mm->pre_commit(trx);
        }
        assert(retval == WSREP_OK || retval == WSREP_TRX_FAIL ||
               retval == WSREP_BF_ABORT);
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }
    mm->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t mm_galera_append_query(wsrep_t *gh,
                                      wsrep_trx_handle_t* trx_handle,
                                      const char *query,
                                      const time_t timeval,
                                      const uint32_t randseed)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));

    TrxHandle* trx(mm->local_trx(trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        trx->append_statement(query, strlen(query), timeval, randseed);
        retval = WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }
    mm->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t mm_galera_append_row_key(wsrep_t *gh,
                                        wsrep_trx_handle_t* trx_handle,
                                        const char    *dbtable,
                                        size_t dbtable_len,
                                        const char *key,
                                        size_t key_len,
                                        enum wsrep_action action)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    TrxHandle* trx(mm->local_trx(trx_handle, true));
    assert(trx != 0);

    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        WriteSet::Action ac;
        switch (action)
        {
        case WSREP_INSERT: ac = WriteSet::A_INSERT;
        case WSREP_UPDATE: ac = WriteSet::A_UPDATE;
        case WSREP_DELETE: ac = WriteSet::A_DELETE;
        }
        trx->append_row_id(dbtable, dbtable_len, key, key_len, ac);
        retval = WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }
    mm->unref_local_trx(trx);

    return retval;
}


extern "C"
wsrep_status_t mm_galera_append_data(wsrep_t*            wsrep,
                                     wsrep_trx_handle_t* trx_handle,
                                     const void*         data,
                                     size_t              data_len)
{
    return WSREP_NOT_IMPLEMENTED;
}

extern "C"
wsrep_status_t mm_galera_causal_read(wsrep_t* wsrep,
                                     wsrep_seqno_t* seqno)
{
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t mm_galera_set_variable(wsrep_t *gh,
                                      const wsrep_conn_id_t  conn_id,
                                      const char *key,
                                      const size_t key_len,
                                      const char *query,
                                      const size_t query_len)
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
wsrep_status_t mm_galera_set_database(wsrep_t *gh,
                                      const wsrep_conn_id_t conn_id,
                                      const char *query,
                                      const size_t query_len)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    try
    {
        if (query != 0)
        {
            mm->set_default_context(conn_id, query, query_len);
        }
        else
        {
            mm->discard_local_conn(conn_id);
        }
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        return WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        return WSREP_FATAL;
    }
}


extern "C"
wsrep_status_t mm_galera_to_execute_start(wsrep_t *gh,
                                          wsrep_conn_id_t conn_id,
                                          const void *query,
                                          size_t query_len,
                                          wsrep_seqno_t* global_seqno)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));

    TrxHandle* trx(mm->local_conn_trx(conn_id, true));
    assert(trx != 0);
    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        trx->append_statement(query, query_len);
        trx->set_flags(TrxHandle::F_COMMIT);
        retval = mm->replicate(trx);
        if (retval == WSREP_OK)
        {
            retval = mm->to_isolation_begin(trx);
        }
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }

    if (retval != WSREP_OK)
    {
        // trx is not needed anymore
        trx->unref();
    }
    else if (global_seqno)
    {
        *global_seqno = trx->global_seqno();
    }
    return retval;
}


extern "C"
wsrep_status_t mm_galera_to_execute_end(wsrep_t *gh,
                                        wsrep_conn_id_t conn_id)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));

    try
    {
        TrxHandle* trx(mm->local_conn_trx(conn_id, false));
        TrxHandleLock lock(*trx);
        mm->to_isolation_end(trx);
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        return WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        return WSREP_FATAL;
    }
}

extern "C"
wsrep_status_t mm_galera_replay_trx(wsrep_t *gh,
                                    wsrep_trx_handle_t* trx_handle,
                                    void *recv_ctx)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    TrxHandle* trx(mm->local_trx(trx_handle, false));
    assert(trx != 0);

    wsrep_status_t retval;
    try
    {
        TrxHandleLock lock(*trx);
        retval = mm->replay(trx, recv_ctx);
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        retval = WSREP_CONN_FAIL;
    }
    catch (...)
    {
        log_fatal << "uncaught exception";
        retval = WSREP_FATAL;
    }
    mm->unref_local_trx(trx);

    return retval;
}

extern "C"
wsrep_status_t mm_galera_sst_sent (wsrep_t* gh,
                                   const wsrep_uuid_t* uuid,
                                   wsrep_seqno_t seqno)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    return mm->sst_sent(*uuid, seqno);
}

extern "C"
wsrep_status_t mm_galera_sst_received (wsrep_t* gh,
                                       const wsrep_uuid_t* uuid,
                                       wsrep_seqno_t seqno,
                                       const char* state,
                                       size_t state_len)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    return mm->sst_received(*uuid, seqno, state, state_len);
}

extern "C"
wsrep_status_t mm_galera_snapshot(wsrep_t*     wsrep,
                                  const void*  msg,
                                  size_t       msg_len,
                                  const char*  donor_spec)
{
    return WSREP_NOT_IMPLEMENTED;
}

extern "C"
struct wsrep_status_var* mm_galera_status_get (wsrep_t* gh)
{
    assert(gh != 0 && gh->ctx != 0);
    MM* mm(reinterpret_cast<MM*>(gh->ctx));
    return const_cast<struct wsrep_status_var*>(mm->status());
}

extern "C"
void mm_galera_status_free (wsrep_t* gh,
                            struct wsrep_status_var* s)
{
}

static wsrep_t mm_galera_str = {
    WSREP_INTERFACE_VERSION,
    &mm_galera_init,
    &mm_galera_options_set,
    &mm_galera_options_get,
    &mm_galera_connect,
    &mm_galera_disconnect,
    &mm_galera_recv,
    &mm_galera_pre_commit,
    &mm_galera_post_commit,
    &mm_galera_post_rollback,
    &mm_galera_replay_trx,
    &mm_galera_abort_pre_commit,
    &mm_galera_abort_slave_trx,
    &mm_galera_append_query,
    &mm_galera_append_row_key,
    &mm_galera_append_data,
    &mm_galera_causal_read,
    &mm_galera_set_variable,
    &mm_galera_set_database,
    &mm_galera_to_execute_start,
    &mm_galera_to_execute_end,
    &mm_galera_sst_sent,
    &mm_galera_sst_received,
    &mm_galera_snapshot,
    &mm_galera_status_get,
    &mm_galera_status_free,
    "Galera",
    "0.8pre",
    "Codership Oy <info@codership.com>",
    &mm_galera_tear_down,
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
        *hptr = mm_galera_str;
    }
    catch (...)
    {
        return ENOTRECOVERABLE;
    }

    return WSREP_OK;
}
