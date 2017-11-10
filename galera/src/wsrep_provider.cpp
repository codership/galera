//
// Copyright (C) 2010-2017 Codership Oy <info@codership.com>
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
using galera::TrxHandlePtr;
using galera::TrxHandleLock;


extern "C" {
    const char* wsrep_interface_version = (char*)WSREP_INTERFACE_VERSION;
}


extern "C"
wsrep_status_t galera_init(wsrep_t* gh, const struct wsrep_init_args* args)
{
    assert(gh != 0);

    try
    {
        gh->ctx = new REPL_CLASS (args);
        // Moved into galera::ReplicatorSMM::ParseOptions::ParseOptions()
        // wsrep_set_params(*reinterpret_cast<REPL_CLASS*>(gh->ctx),
        //                 args->options);
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << e.what();
    }
#ifdef NDEBUG
    catch (std::exception& e)
    {
        log_error << e.what();
    }
    catch (gu::NotFound& e)
    {
        /* Unrecognized parameter (logged by gu::Config::set()) */
    }
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
    catch (gu::Exception& e)
    {
        log_error << "Failed to connect to cluster: "
                  << e.what();
        return WSREP_NODE_FAIL;
    }
#ifdef NDEBUG
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
#endif // ! NDEBUG
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
    TrxHandle* trx(0);

    assert(handle != 0);

    if (handle->opaque != 0)
    {
        trx = static_cast<TrxHandle*>(handle->opaque);
        assert(trx->trx_id() == handle->trx_id ||
               wsrep_trx_id_t(-1) == handle->trx_id);
        assert(trx->get_shared_ptr().get() == trx);
    }
    else
    {
        try
        {
            TrxHandlePtr txp(repl->get_local_trx(handle->trx_id, create));
            trx = txp.get();
            assert(!trx || create);
            if (trx) trx->set_shared_ptr(txp);
            handle->opaque = trx;
        }
        catch (gu::NotFound& )
        {}
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

    log_debug << "replaying " << *trx;
    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->replay_trx(trx->get_shared_ptr(), recv_ctx);
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

    if (retval != WSREP_OK)
    {
        log_debug << "replaying failed for " << *trx;
    }
    return retval;
}


extern "C"
wsrep_status_t galera_abort_certification(wsrep_t*       gh,
                                          wsrep_seqno_t  bf_seqno,
                                          wsrep_trx_id_t victim_trx,
                                          wsrep_seqno_t* victim_seqno)
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    assert(victim_seqno != 0);

    *victim_seqno = WSREP_SEQNO_UNDEFINED;

    REPL_CLASS *     repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    wsrep_status_t   retval;
    galera::TrxHandlePtr trx(repl->get_local_trx(victim_trx));

    if (!trx)
    {
        log_warn << "trx to abort " << victim_trx
                 << " with bf seqno " << bf_seqno
                 << " not found";
        return WSREP_OK;
    }
    else
    {
        log_debug << "ABORTING trx " << victim_trx
                  << " with bf seqno " << bf_seqno;
    }

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->abort_trx(trx.get(), bf_seqno, victim_seqno);
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

    GU_DBUG_SYNC_WAIT("abort_trx_end");

    return retval;
}

extern "C"
wsrep_status_t galera_rollback(wsrep_t*                 gh,
                               wsrep_trx_id_t           trx_id,
                               const wsrep_buf_t* const data)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    galera::TrxHandlePtr victim(repl->get_local_trx(trx_id));

    if (!victim)
    {
        log_warn << "trx to rollback " << trx_id << " not found";
        return WSREP_OK;
    }

    /* Send the rollback fragment from a different context */
    galera::TrxHandlePtr trx(repl->new_local_trx(trx_id));

    TrxHandleLock lock(*trx);
    if (data)
    {
        gu_trace(trx->append_data(data->ptr, data->len,
                                  WSREP_DATA_ORDERED, true));
    }
    wsrep_trx_meta_t meta;
    meta.gtid       = WSREP_GTID_UNDEFINED;
    meta.depends_on = WSREP_SEQNO_UNDEFINED;
    meta.stid.node  = repl->source_id();
    meta.stid.trx   = trx_id;

    trx->set_flags(TrxHandle::F_ROLLBACK);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);
    return repl->send(trx.get(), &meta);
}

static inline void
discard_local_trx(REPL_CLASS*        repl,
                  wsrep_ws_handle_t* ws_handle,
                  TrxHandle*         trx)
{
    repl->discard_local_trx(trx);
    trx->set_shared_ptr(TrxHandlePtr());
    ws_handle->opaque = 0;
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
        gu_trace(trx->append_data(data[i].ptr, data[i].len, type, copy));
    }
}


extern "C"
wsrep_status_t galera_assign_read_view(wsrep_t*           const  gh,
                                       wsrep_ws_handle_t* const  handle,
                                       const wsrep_gtid_t* const rv)
{
    return WSREP_NOT_IMPLEMENTED;
}


extern "C"
wsrep_status_t galera_certify(wsrep_t*           const gh,
                              wsrep_conn_id_t    const conn_id,
                              wsrep_ws_handle_t* const trx_handle,
                              uint32_t           const flags,
                              wsrep_trx_meta_t*  const meta)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandle* trx(get_local_trx(repl, trx_handle, /*rbr_data != 0*/ false));

    // TRX_START and ROLLBACK flags should not be set together
    assert((flags & (WSREP_FLAG_TRX_START | WSREP_FLAG_ROLLBACK))
           != (WSREP_FLAG_TRX_START | WSREP_FLAG_ROLLBACK));

    if (gu_unlikely(trx == 0))
    {
        if (meta != 0)
        {
            meta->gtid       = WSREP_GTID_UNDEFINED;
            meta->depends_on = WSREP_SEQNO_UNDEFINED;
            meta->stid.node  = repl->source_id();
            meta->stid.trx   = -1;
        }
        // no data to replicate
        return WSREP_OK;
    }

    assert(trx->trx_id() != uint64_t(-1));

    if (meta != 0)
    {
        meta->gtid       = WSREP_GTID_UNDEFINED;
        meta->depends_on = WSREP_SEQNO_UNDEFINED;
        meta->stid.node  = trx->source_id();
        meta->stid.trx   = trx->trx_id();
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);

        trx->set_conn_id(conn_id);

        trx->set_flags(trx->flags() |
                       TrxHandle::wsrep_flags_to_trx_flags(flags));

        retval = repl->replicate(trx->get_shared_ptr(), meta);

        if (meta)
        {
            if (trx->write_set_in().size() > 0)
            {
                assert(meta->gtid.seqno > 0);
                assert(meta->gtid.seqno == trx->global_seqno());
                assert(meta->depends_on == trx->depends_seqno());
            }
            else
            {
                assert(meta->gtid.seqno == WSREP_SEQNO_UNDEFINED);
                assert(meta->depends_on == WSREP_SEQNO_UNDEFINED);
            }
        }

        assert(trx->trx_id() == meta->stid.trx);
        assert(!(retval == WSREP_OK || retval == WSREP_BF_ABORT) ||
               (trx->global_seqno() > 0));

        if (retval == WSREP_OK)
        {
            assert(trx->state() != TrxHandle::S_MUST_ABORT);

            if ((flags & WSREP_FLAG_ROLLBACK) == 0)
            {
                assert(trx->last_seen_seqno() >= 0);
                retval = repl->certify(trx->get_shared_ptr(), meta);
                assert(trx->state() != TrxHandle::S_MUST_ABORT ||
                       retval != WSREP_OK);
                if (meta) assert(meta->depends_on >= 0 || retval != WSREP_OK);
            }
        }
        else
        {
            if (meta) meta->depends_on = -1;
        }

        assert(retval == WSREP_OK ||       // success
               retval == WSREP_TRX_FAIL || // cert failure
               retval == WSREP_BF_ABORT || // BF abort
               retval == WSREP_CONN_FAIL); // not in joined/synced state
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

    return retval;
}


extern "C"
wsrep_status_t galera_commit_order_enter(
    wsrep_t*                 const gh,
    const wsrep_ws_handle_t* const ws_handle
    )
{
    assert(gh        != 0);
    assert(gh->ctx   != 0);
    assert(ws_handle != 0);

    REPL_CLASS * const repl(static_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* const trx(static_cast<TrxHandle*>(ws_handle->opaque));
    assert(NULL != trx);
    if (trx == 0)
    {
        log_warn << "Trx " << ws_handle->trx_id
                 << " not found for commit order enter";
        return WSREP_TRX_MISSING;
    }

    wsrep_status_t retval;

    try
    {
        if (trx->is_local() && trx->state() != TrxHandle::S_REPLAYING)
        {
            TrxHandleLock lock(*trx);

            if (gu_unlikely(trx->state() == TrxHandle::S_MUST_ABORT))
            {
                trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
                return WSREP_BF_ABORT;
            }

            retval = repl->commit_order_enter_local(*trx);
        }
        else
        {
            /* IST writesets need not be locked */
            assert(trx->owned() || trx->local_seqno() == WSREP_SEQNO_UNDEFINED);
            retval = repl->commit_order_enter_remote(*trx);
        }
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

    return retval;
}

extern "C"
wsrep_status_t galera_commit_order_leave(
    wsrep_t*                 const gh,
    const wsrep_ws_handle_t* const ws_handle,
    const wsrep_buf_t*       const error
    )
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    assert(ws_handle != 0);

    REPL_CLASS * const repl(static_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* const trx(static_cast<TrxHandle*>(ws_handle->opaque));
    assert(NULL != trx);

    if (trx == 0)
    {
        log_warn << "Trx " << ws_handle->trx_id
                 << " not found for commit order leave";
        return WSREP_TRX_MISSING;
    }

    wsrep_status_t retval;

    try
    {
        if (trx->is_local() && trx->state() != TrxHandle::S_REPLAYING)
        {
            TrxHandleLock lock(*trx);
            retval = repl->commit_order_leave(*trx, error);
        }
        else
        {
            /* IST writesets need not be locked */
            assert(trx->owned() || trx->local_seqno() == WSREP_SEQNO_UNDEFINED);
            retval = repl->commit_order_leave(*trx, error);
        }
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

    return retval;
}


extern "C"
wsrep_status_t galera_release(wsrep_t*            gh,
                              wsrep_ws_handle_t*  ws_handle)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* trx(get_local_trx(repl, ws_handle, false));

    if (trx == 0)
    {
        log_debug << "trx " << ws_handle->trx_id
                  << " not found for release";
        return WSREP_OK;
    }

    wsrep_status_t retval;
    TrxHandle::State trx_state(TrxHandle::S_EXECUTING);

    try
    {
        TrxHandleLock lock(*trx);

        if (gu_likely(trx->state() == TrxHandle::S_COMMITTED))
            retval = repl->release_commit(*trx);
        else
            retval = repl->release_rollback(*trx);

        trx_state = trx->state();
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

    switch(trx_state)
    {
    case TrxHandle::S_COMMITTED:
    case TrxHandle::S_ROLLED_BACK:
        discard_local_trx(repl, ws_handle, trx);
        break;
    default:
        assert(0);
    }

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
            gu_trace(trx->append_key(k));
        }
        retval = WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        if (EMSGSIZE == e.get_errno())
            retval = WSREP_SIZE_EXCEEDED;
        else
            retval = WSREP_CONN_FAIL; //?
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
        gu_trace(append_data_array(trx, data, count, type, copy));
        retval = WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        if (EMSGSIZE == e.get_errno())
            retval = WSREP_SIZE_EXCEEDED;
        else
            retval = WSREP_CONN_FAIL; //?
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
wsrep_status_t galera_sync_wait(wsrep_t*      const wsrep,
                                wsrep_gtid_t* const upto,
                                int                 tout,
                                wsrep_gtid_t* const gtid)
{
    assert(wsrep != 0);
    assert(wsrep->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(wsrep->ctx));
    wsrep_status_t retval;
    try
    {
        retval = repl->sync_wait(upto, tout, gtid);
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
wsrep_status_t galera_last_committed_id(wsrep_t*      const wsrep,
                                        wsrep_gtid_t* const gtid)
{
    assert(wsrep != 0);
    assert(wsrep->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(wsrep->ctx));
    wsrep_status_t retval;
    try
    {
        retval = repl->last_committed_id(gtid);
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
                                       uint32_t                const flags,
                                       wsrep_trx_meta_t*       const meta)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    TrxHandlePtr txp(repl->local_conn_trx(conn_id, true));
    assert(txp != 0);

    TrxHandle* trx(txp.get());
    assert(trx->state() == TrxHandle::S_EXECUTING);

    trx->set_flags(TrxHandle::wsrep_flags_to_trx_flags(
                       flags | WSREP_FLAG_ISOLATION));

    if (meta != 0)
    {
        meta->gtid       = WSREP_GTID_UNDEFINED;
        meta->depends_on = WSREP_SEQNO_UNDEFINED;
        meta->stid.node  = trx->source_id();
        meta->stid.trx   = trx->trx_id();
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);

        trx->set_flags(TrxHandle::wsrep_flags_to_trx_flags(
                           WSREP_FLAG_TRX_END |
                           WSREP_FLAG_ISOLATION));

        for (size_t i(0); i < keys_num; ++i)
        {
            galera::KeyData k(repl->trx_proto_ver(),
                              keys[i].key_parts,
                              keys[i].key_parts_num, WSREP_KEY_EXCLUSIVE,false);
            gu_trace(trx->append_key(k));
        }

        gu_trace(append_data_array(trx, data, count, WSREP_DATA_ORDERED, false));

        retval = repl->replicate(txp, meta);

        assert((retval == WSREP_OK && trx->global_seqno() > 0) ||
               (retval != WSREP_OK && trx->global_seqno() < 0));

        if (meta)
        {
            if (WSREP_OK == retval)
            {
                assert(meta->gtid.seqno > 0);
                assert(meta->gtid.seqno == trx->global_seqno());
                assert(meta->depends_on == trx->depends_seqno());
            }
            else
            {
                assert(meta->gtid.seqno == WSREP_SEQNO_UNDEFINED);
                assert(meta->depends_on == WSREP_SEQNO_UNDEFINED);
            }
        }

        if (retval == WSREP_OK)
        {
            retval = repl->to_isolation_begin(txp, meta);
        }
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();

        if (e.get_errno() == EMSGSIZE)
            retval = WSREP_SIZE_EXCEEDED;
        else
            retval = WSREP_CONN_FAIL;
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

    if (trx->global_seqno() < 0)
    {
        // galera_to_execute_end() won't be called
        repl->discard_local_conn_trx(conn_id); // trx is not needed anymore
    }

    return retval;
}


extern "C"
wsrep_status_t galera_to_execute_end(wsrep_t*           const gh,
                                     wsrep_conn_id_t    const conn_id,
                                     const wsrep_buf_t* const err)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    wsrep_status_t retval;
    TrxHandlePtr txp(repl->local_conn_trx(conn_id, false));
    TrxHandle* trx(txp.get());

    assert(trx != 0);
    if (trx == 0)
    {
        log_warn << "No trx handle for connection " << conn_id
                 << " in galera_to_execute_end()";
        return WSREP_CONN_FAIL;
    }

    try
    {
        TrxHandleLock lock(*trx);
        gu_trace(repl->to_isolation_end(txp, err));
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

    gu_trace(repl->discard_local_conn_trx(conn_id));

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
galera_preordered_commit (wsrep_t*            const gh,
                          wsrep_po_handle_t*  const handle,
                          const wsrep_uuid_t* const source_id,
                          uint32_t            const flags,
                          int                 const pa_range,
                          wsrep_bool_t        const commit)
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
                                    const wsrep_buf_t*  const state,
                                    int                 const rcode)
{
    assert(gh       != 0);
    assert(gh->ctx  != 0);
    assert(state_id != 0);
    assert(rcode    <= 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    if (rcode < 0) { assert(state_id->seqno == WSREP_SEQNO_UNDEFINED); }

    return repl->sst_received(*state_id, state, rcode);
}


extern "C"
wsrep_status_t galera_snapshot(wsrep_t*           const wsrep,
                               const wsrep_buf_t* const msg,
                               const char*        const donor_spec)
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
    assert(gh != 0);
    assert(gh->ctx != 0);
    REPL_CLASS* repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    return repl->stats_free(s);
    //REPL_CLASS::stats_free(s);
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
    &galera_assign_read_view,
    &galera_certify,
    &galera_commit_order_enter,
    &galera_commit_order_leave,
    &galera_release,
    &galera_replay_trx,
    &galera_abort_certification,
    &galera_rollback,
    &galera_append_key,
    &galera_append_data,
    &galera_sync_wait,
    &galera_last_committed_id,
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
