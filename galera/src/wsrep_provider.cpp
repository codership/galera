//
// Copyright (C) 2010-2017 Codership Oy <info@codership.com>
//

#include "key_data.hpp"
#include "gu_serialize.hpp"

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
using galera::TrxHandleMaster;
using galera::TrxHandleSlave;
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
wsrep_cap_t galera_capabilities(wsrep_t* gh)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    return repl->capabilities();
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
wsrep_status_t galera_enc_set_key(wsrep_t* gh, const wsrep_enc_key_t*key)
{
    return WSREP_NOT_IMPLEMENTED;
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
#endif /* NDEBUG */
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

    return WSREP_FATAL;
#endif /* NDEBUG */
}

static TrxHandleMaster*
get_local_trx(REPL_CLASS* const        repl,
              wsrep_ws_handle_t* const handle,
              bool const               create)
{
    TrxHandleMaster* trx(0);

    assert(handle != 0);

    if (handle->opaque != 0)
    {
        trx = static_cast<TrxHandleMaster*>(handle->opaque);
        assert(trx->trx_id() == handle->trx_id ||
               wsrep_trx_id_t(-1) == handle->trx_id);
    }
    else
    {
        try
        {
            trx = repl->get_local_trx(handle->trx_id, create).get();
            handle->opaque = trx;
        }
        catch (gu::NotFound& ) { }
    }

    return trx;
}

extern "C"
wsrep_status_t galera_replay_trx(wsrep_t*                  gh,
                                 const wsrep_ws_handle_t*  trx_handle,
                                 void*                     recv_ctx)
{
    assert(gh != 0);
    assert(gh->ctx != 0);

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));
    TrxHandleMaster* trx(static_cast<TrxHandleMaster*>(trx_handle->opaque));
    assert(trx != 0);
    assert(trx->ts() != 0);
    log_debug << "replaying " << *(trx->ts());
    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(*trx);
        retval = repl->replay_trx(*trx, lock, recv_ctx);
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
        log_debug << "replaying failed for " << *(trx->ts());
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
    galera::TrxHandleMasterPtr txp(repl->get_local_trx(victim_trx));

    if (!txp)
    {
        log_debug << "trx to abort " << victim_trx
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
        TrxHandleMaster& trx(*txp);
        TrxHandleLock lock(trx);
        retval = repl->abort_trx(trx, bf_seqno, victim_seqno);
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
    galera::TrxHandleMasterPtr victim(repl->get_local_trx(trx_id));

    if (!victim)
    {
        log_debug << "trx to rollback " << trx_id << " not found";
        return WSREP_OK;
    }

    TrxHandleLock victim_lock(*victim);

    /* Send the rollback fragment from a different context */
    galera::TrxHandleMasterPtr trx(repl->new_local_trx(trx_id));

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

    trx->set_flags(TrxHandle::EXPLICIT_ROLLBACK_FLAGS);
    trx->set_state(TrxHandle::S_MUST_ABORT);
    trx->set_state(TrxHandle::S_ABORTING);

    // Victim may already be in S_ABORTING state if it was BF aborted
    // in pre commit.
    if (victim->state() != TrxHandle::S_ABORTING)
    {
        if (victim->state() != TrxHandle::S_MUST_ABORT)
            victim->set_state(TrxHandle::S_MUST_ABORT);
        victim->set_state(TrxHandle::S_ABORTING);
    }

    return repl->send(*trx, &meta);
}

static inline void
discard_local_trx(REPL_CLASS*        repl,
                  wsrep_ws_handle_t* ws_handle,
                  TrxHandleMaster*   trx)
{
    repl->discard_local_trx(trx);
    ws_handle->opaque = 0;
}

static inline void
append_data_array (TrxHandleMaster&              trx,
                   const struct wsrep_buf* const data,
                   size_t                  const count,
                   wsrep_data_type_t       const type,
                   bool                    const copy)
{
    for (size_t i(0); i < count; ++i)
    {
        gu_trace(trx.append_data(data[i].ptr, data[i].len, type, copy));
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

    REPL_CLASS * const repl(static_cast< REPL_CLASS * >(gh->ctx));

    TrxHandleMaster* txp(get_local_trx(repl, trx_handle, false));

    // The following combinations of flags should not be set together
    assert(!((flags & WSREP_FLAG_TRX_START) &&
             (flags & WSREP_FLAG_ROLLBACK)));

    assert(!((flags & WSREP_FLAG_TRX_PREPARE) &&
             (flags & WSREP_FLAG_ROLLBACK)));

    assert(!((flags & WSREP_FLAG_TRX_PREPARE) &&
             (flags & WSREP_FLAG_TRX_END)));

    if (gu_unlikely(txp == 0))
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

    TrxHandleMaster& trx(*txp);

    assert(trx.trx_id() != uint64_t(-1));

    if (meta != 0)
    {
        meta->gtid       = WSREP_GTID_UNDEFINED;
        meta->depends_on = WSREP_SEQNO_UNDEFINED;
        meta->stid.node  = trx.source_id();
        meta->stid.trx   = trx.trx_id();
    }

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(trx);

        trx.set_conn_id(conn_id);

        trx.set_flags(trx.flags() |
                       TrxHandle::wsrep_flags_to_trx_flags(flags));

        if (flags & WSREP_FLAG_ROLLBACK)
        {
            if ((trx.flags() & (TrxHandle::F_BEGIN | TrxHandle::F_ROLLBACK)) ==
                (TrxHandle::F_BEGIN | TrxHandle::F_ROLLBACK))
            {
                return WSREP_TRX_MISSING;
            }

            trx.set_flags(trx.flags() | TrxHandle::F_PA_UNSAFE);
            if (trx.state() == TrxHandle::S_ABORTING)
            {
                trx.set_state(TrxHandle::S_EXECUTING);
            }
        }

        retval = repl->replicate(trx, meta);

        if (meta)
        {
            if (trx.ts())
            {
                assert(meta->gtid.seqno > 0);
                assert(meta->gtid.seqno == trx.ts()->global_seqno());
                assert(meta->depends_on == trx.ts()->depends_seqno());
            }
            else
            {
                assert(meta->gtid.seqno == WSREP_SEQNO_UNDEFINED);
                assert(meta->depends_on == WSREP_SEQNO_UNDEFINED);
            }
        }

        assert(trx.trx_id() == meta->stid.trx);
        assert(!(retval == WSREP_OK || retval == WSREP_BF_ABORT) ||
               (trx.ts() && trx.ts()->global_seqno() > 0));

        if (retval == WSREP_OK)
        {
            assert(trx.state() != TrxHandle::S_MUST_ABORT);

            if ((flags & WSREP_FLAG_ROLLBACK) == 0)
            {
                assert(trx.ts() && trx.ts()->last_seen_seqno() >= 0);
                retval = repl->certify(trx, meta);
                assert(trx.state() != TrxHandle::S_MUST_ABORT ||
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
               retval == WSREP_CONN_FAIL|| // not in joined/synced state
               retval == WSREP_NODE_FAIL); // node inconsistent
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

    trx.release_write_set_out();

    return retval;
}


extern "C"
wsrep_status_t galera_commit_order_enter(
    wsrep_t*                 const gh,
    const wsrep_ws_handle_t* const ws_handle,
    const wsrep_trx_meta_t*  const meta
    )
{
    assert(gh        != 0);
    assert(gh->ctx   != 0);
    assert(ws_handle != 0);

    REPL_CLASS * const repl(static_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* const txp(static_cast<TrxHandle*>(ws_handle->opaque));
    assert(NULL != txp);
    if (txp == 0)
    {
        log_warn << "Trx " << ws_handle->trx_id
                 << " not found for commit order enter";
        return WSREP_TRX_MISSING;
    }

    wsrep_status_t retval;

    try
    {
        if (txp->master())
        {
            TrxHandleMaster& trx(*reinterpret_cast<TrxHandleMaster*>(txp));
            TrxHandleLock lock(trx);

            // assert(trx.state() != TrxHandle::S_REPLAYING);

            if (gu_unlikely(trx.state() == TrxHandle::S_MUST_ABORT))
            {
                if (trx.ts() && (trx.ts()->flags() & TrxHandle::F_COMMIT))
                {
                    trx.set_state(TrxHandle::S_MUST_REPLAY);
                    return WSREP_BF_ABORT;
                }
                else
                {
                    trx.set_state(TrxHandle::S_ABORTING);
                    return WSREP_TRX_FAIL;
                }
            }

            retval = repl->commit_order_enter_local(trx);
        }
        else
        {
            TrxHandleSlave& ts(*reinterpret_cast<TrxHandleSlave*>(txp));
            retval = repl->commit_order_enter_remote(ts);
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
    const wsrep_trx_meta_t*  const meta,
    const wsrep_buf_t*       const error
    )
{
    assert(gh != 0);
    assert(gh->ctx != 0);
    assert(ws_handle != 0);

    REPL_CLASS * const repl(static_cast< REPL_CLASS * >(gh->ctx));
    TrxHandle* const txp(static_cast<TrxHandle*>(ws_handle->opaque));
    assert(NULL != txp);

    if (txp == NULL)
    {
        log_warn << "Trx " << ws_handle->trx_id
                 << " not found for commit order leave";
        return WSREP_TRX_MISSING;
    }

    wsrep_status_t retval;

    try
    {
        if (txp->master())
        {
            TrxHandleMaster& trx(*reinterpret_cast<TrxHandleMaster*>(txp));
            TrxHandleLock lock(trx);
            assert(trx.ts() && trx.ts()->global_seqno() > 0);

            if (trx.state() == TrxHandle::S_MUST_ABORT)
            {
                // Trx is non-committing streaming replication and
                // the trx was BF aborted while committing a fragment.
                // At this point however, we can't know if the
                // fragment is already committed into DBMS fragment storage
                // or not, so we return a success. The BF abort error
                // is returned to the caller from galera_release().
                assert(!(trx.ts()->flags() & TrxHandle::F_COMMIT));
                trx.set_state(TrxHandle::S_ABORTING);
                retval = repl->commit_order_leave(*trx.ts(), error);
                trx.set_deferred_abort(true);
            }
            else
            {
                retval = repl->commit_order_leave(*trx.ts(), error);
                assert(trx.state() == TrxHandle::S_ROLLING_BACK ||
                       trx.state() == TrxHandle::S_COMMITTING ||
                       !(trx.ts()->flags() & TrxHandle::F_COMMIT));
                trx.set_state(trx.state() == TrxHandle::S_ROLLING_BACK ?
                              TrxHandle::S_ROLLED_BACK :
                              TrxHandle::S_COMMITTED);
            }
        }
        else
        {
            TrxHandleSlave& ts(*reinterpret_cast<TrxHandleSlave*>(txp));
            retval = repl->commit_order_leave(ts, error);
        }
    }
    catch (std::exception& e)
    {
        log_error << "commit_order_leave(): " << e.what();
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "commit_order_leave(): non-standard exception";
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
    TrxHandleMaster* txp(get_local_trx(repl, ws_handle, false));

    if (txp == 0)
    {
        log_debug << "trx " << ws_handle->trx_id
                  << " not found for release";
        return WSREP_OK;
    }

    wsrep_status_t retval;
    bool discard_trx(true);

    try
    {
        TrxHandleMaster& trx(*txp);
        TrxHandleLock lock(trx);

        if (trx.state() == TrxHandle::S_MUST_ABORT)
        {
            // This is possible in case of ALG due to a race: BF applier BF
            // aborts trx that has already grabbed commit monitor and is
            // committing. This is possible only if aborter is ordered after
            // the victim, and since for regular committing transactions such
            // abort is unnecessary, this should be possible only for ongoing
            // streaming transactions.

            galera::TrxHandleSlavePtr ts(trx.ts());

            if (ts && ts->flags() & TrxHandle::F_COMMIT)
            {
                log_warn << "trx was BF aborted during commit: " << *ts;
                assert(0);
                // manipulate state to avoid crash
                trx.set_state(TrxHandle::S_MUST_REPLAY);
                trx.set_state(TrxHandle::S_REPLAYING);
            }
            else
            {
                // Streaming replication, not in commit phase. Must abort.
                log_debug << "SR trx was BF aborted during commit: " << trx;
                trx.set_state(TrxHandle::S_ABORTING);
            }
        }

        if (gu_likely(trx.state() == TrxHandle::S_COMMITTED))
        {
            assert(!trx.deferred_abort());
            retval = repl->release_commit(trx);

            assert(trx.state() == TrxHandle::S_COMMITTED ||
                   trx.state() == TrxHandle::S_EXECUTING);

            if (trx.state() == TrxHandle::S_EXECUTING &&
                retval == WSREP_OK)
            {
                // SR trx ready for new fragment, keep transaction
                discard_trx = false;
            }
        }
        else if (trx.deferred_abort() == false)
        {
            retval = repl->release_rollback(trx);

            assert(trx.state() == TrxHandle::S_ROLLED_BACK);
        }
        else if (trx.state() == TrxHandle::S_ABORTING)
        {
            assert(trx.deferred_abort());
            // SR trx was BF aborted before commit_order_leave()
            // We return BF abort error code here and do not clean up
            // the transaction. The transaction is needed for sending
            // rollback fragment.
            retval = WSREP_BF_ABORT;
            discard_trx = false;
            trx.set_deferred_abort(false);
        }
        else
        {
            assert(0);
            gu_throw_fatal << "Internal program error: "
                "unexpected state in deferred abort trx: " << trx;
        }

        switch(trx.state())
        {
        case TrxHandle::S_COMMITTED:
        case TrxHandle::S_ROLLED_BACK:
        case TrxHandle::S_EXECUTING:
        case TrxHandle::S_ABORTING:
            break;
        default:
            assert(0);
            gu_throw_fatal << "Internal library error: "
                "unexpected trx release state: " << trx;
        }
    }
    catch (std::exception& e)
    {
        log_error << e.what();
        assert(0);
        retval = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "non-standard exception";
        assert(0);
        retval = WSREP_FATAL;
    }

    if (discard_trx)
    {
        discard_local_trx(repl, ws_handle, txp);
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
    TrxHandleMaster* trx(get_local_trx(repl, trx_handle, true));
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
    TrxHandleMaster* txp(get_local_trx(repl, trx_handle, true));
    assert(txp != 0);
    TrxHandleMaster& trx(*txp);

    wsrep_status_t retval;

    try
    {
        TrxHandleLock lock(trx);
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
    // This function is now no-op and can be removed from the
    // future versions. Connection object is allocated only from
    // galera_to_execute_start() and will be released either
    // from that function in case of failure or from
    // galera_to_execute_end().
    return WSREP_OK;
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

    // Non-blocking operations "certification" depends on
    // TRX_START and TRX_END flags, not having those flags may cause
    // undefined behavior so check them here.
    assert(flags & (WSREP_FLAG_TRX_START | WSREP_FLAG_TRX_END));

    if ((flags & (WSREP_FLAG_TRX_START | WSREP_FLAG_TRX_END)) == 0)
    {
        log_warn << "to_execute_start(): either WSREP_FLAG_TRX_START "
                 << "or WSREP_FLAG_TRX_END flag is required";
        return WSREP_CONN_FAIL;
    }

    // Simultaneous use of TRX_END AND ROLLBACK is not allowed
    assert(!((flags & WSREP_FLAG_TRX_END) && (flags & WSREP_FLAG_ROLLBACK)));

    if ((flags & WSREP_FLAG_TRX_END) && (flags & WSREP_FLAG_ROLLBACK))
    {
        log_warn << "to_execute_start(): simultaneous use of "
                 << "WSREP_FLAG_TRX_END and WSREP_FLAG_ROLLBACK "
                 << "is not allowed";
        return WSREP_CONN_FAIL;
    }

    REPL_CLASS * repl(reinterpret_cast< REPL_CLASS * >(gh->ctx));

    galera::TrxHandleMasterPtr txp(repl->local_conn_trx(conn_id, true));
    assert(txp != 0);

    TrxHandleMaster& trx(*txp.get());
    assert(trx.state() == TrxHandle::S_EXECUTING);

    trx.set_flags(TrxHandle::wsrep_flags_to_trx_flags(
                      flags | WSREP_FLAG_ISOLATION));


    // NBO-end event. Application should have provided the ongoing
    // operation start event source node id and connection id in
    // meta->stid.node and meta->stid.conn respectively
    if (trx.nbo_end() == true)
    {
        galera::NBOKey key(meta->gtid.seqno);
        gu::Buffer buf(galera::NBOKey::serial_size());
        (void)key.serialize(&buf[0], buf.size(), 0);
        struct wsrep_buf data_buf = {&buf[0], buf.size()};
        gu_trace(append_data_array(trx, &data_buf, 1, WSREP_DATA_ORDERED,true));
    }

    if (meta != 0)
    {
        // Don't override trx meta gtid for NBO end yet, gtid is used in
        // replicator wait_nbo_end() to locate correct nbo context
        if (trx.nbo_end() == false)
        {
            meta->gtid       = WSREP_GTID_UNDEFINED;
        }
        meta->depends_on = WSREP_SEQNO_UNDEFINED;
        meta->stid.node  = trx.source_id();
        meta->stid.trx   = trx.trx_id();
        meta->stid.conn  = trx.conn_id();
    }

    wsrep_status_t retval;

#ifdef NDEBUG
    try
#endif // NDEBUG
    {
        TrxHandleLock lock(trx);
        for (size_t i(0); i < keys_num; ++i)
        {
            galera::KeyData k(repl->trx_proto_ver(),
                              keys[i].key_parts,
                              keys[i].key_parts_num, WSREP_KEY_EXCLUSIVE,false);
            gu_trace(trx.append_key(k));
        }

        gu_trace(append_data_array(trx, data, count, WSREP_DATA_ORDERED, false));

        if (trx.nbo_end() == false)
        {
            retval = repl->replicate(trx, meta);
            assert((retval == WSREP_OK && trx.ts() != 0 &&
                    trx.ts()->global_seqno() > 0) ||
                   (retval != WSREP_OK && (trx.ts() == 0  ||
                                           trx.ts()->global_seqno() < 0)));
            if (meta)
            {
                if (trx.ts())
                {
                    assert(meta->gtid.seqno > 0);
                    assert(meta->gtid.seqno == trx.ts()->global_seqno());
                    assert(meta->depends_on == trx.ts()->depends_seqno());
                }
                else
                {
                    assert(meta->gtid.seqno == WSREP_SEQNO_UNDEFINED);
                    assert(meta->depends_on == WSREP_SEQNO_UNDEFINED);
                }
            }
        }
        else
        {
            // NBO-end events are broadcasted separately in to_isolation_begin()
            retval = WSREP_OK;
        }

        if (retval == WSREP_OK)
        {
            retval = repl->to_isolation_begin(trx, meta);
        }
    }
#ifdef NDEBUG
    catch (gu::Exception& e)
    {
        log_error << e.what();

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
#endif // NDEBUG

    if (trx.ts() == NULL || trx.ts()->global_seqno() < 0)
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

    galera::TrxHandleMasterPtr trx(repl->local_conn_trx(conn_id, false));

    assert(trx != 0);
    if (trx == 0)
    {
        log_warn << "No trx handle for connection " << conn_id
                 << " in galera_to_execute_end()";
        return WSREP_CONN_FAIL;
    }

    wsrep_status_t ret(WSREP_OK);
    try
    {
        TrxHandleLock lock(*trx);
        repl->to_isolation_end(*trx, err);
    }
    catch (std::exception& e)
    {
        log_error << "to_execute_end(): " << e.what();
        ret = WSREP_NODE_FAIL;
    }
    catch (...)
    {
        log_fatal << "to_execute_end(): non-standard exception";
        ret = WSREP_FATAL;
    }
    gu_trace(repl->discard_local_conn_trx(conn_id));


    // trx will be unreferenced (destructed) during purge
    repl->discard_local_conn_trx(conn_id);
    return ret;
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
    &galera_enc_set_key,
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
