//
// Copyright (C) 2010-2017 Codership Oy <info@codership.com>
//

#include "galera_common.hpp"
#include "replicator_smm.hpp"
#include "gcs_action_source.hpp"
#include "galera_exception.hpp"

#include "galera_info.hpp"

#include <gu_debug_sync.hpp>
#include <gu_abort.h>

#include <sstream>
#include <iostream>

std::ostream& galera::operator<<(std::ostream& os, ReplicatorSMM::State state)
{
    switch (state)
    {
    case ReplicatorSMM::S_DESTROYED: return (os << "DESTROYED");
    case ReplicatorSMM::S_CLOSED:    return (os << "CLOSED");
    case ReplicatorSMM::S_CONNECTED: return (os << "CONNECTED");
    case ReplicatorSMM::S_JOINING:   return (os << "JOINING");
    case ReplicatorSMM::S_JOINED:    return (os << "JOINED");
    case ReplicatorSMM::S_SYNCED:    return (os << "SYNCED");
    case ReplicatorSMM::S_DONOR:     return (os << "DONOR");
    }

    gu_throw_fatal << "invalid state " << static_cast<int>(state);
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//                           Public
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

galera::ReplicatorSMM::ReplicatorSMM(const struct wsrep_init_args* args)
    :
    ist_event_queue_    (),
    init_lib_           (reinterpret_cast<gu_log_cb_t>(args->logger_cb)),
    config_             (),
    init_config_        (config_, args->node_address, args->data_dir),
    parse_options_      (*this, config_, args->options),
    init_ssl_           (config_),
    str_proto_ver_      (-1),
    protocol_version_   (-1),
    proto_max_          (gu::from_string<int>(config_.get(Param::proto_max))),
    state_              (S_CLOSED),
    closing_mutex_      (),
    closing_cond_       (),
    closing_            (false),
    sst_state_          (SST_NONE),
    co_mode_            (CommitOrder::from_string(
                             config_.get(Param::commit_order))),
    state_file_         (config_.get(BASE_DIR)+'/'+GALERA_STATE_FILE),
    st_                 (state_file_),
    safe_to_bootstrap_  (true),
    trx_params_         (config_.get(BASE_DIR), -1,
                         KeySet::version(config_.get(Param::key_format)),
                         gu::from_string<int>(config_.get(
                             Param::max_write_set_size))),
    uuid_               (WSREP_UUID_UNDEFINED),
    state_uuid_         (WSREP_UUID_UNDEFINED),
    state_uuid_str_     (),
    cc_seqno_           (WSREP_SEQNO_UNDEFINED),
    cc_lowest_trx_seqno_(WSREP_SEQNO_UNDEFINED),
    pause_seqno_        (WSREP_SEQNO_UNDEFINED),
    app_ctx_            (args->app_ctx),
    connected_cb_       (args->connected_cb),
    view_cb_            (args->view_cb),
    sst_request_cb_     (args->sst_request_cb),
    apply_cb_           (args->apply_cb),
    unordered_cb_       (args->unordered_cb),
    sst_donate_cb_      (args->sst_donate_cb),
    synced_cb_          (args->synced_cb),
    sst_donor_          (),
    sst_uuid_           (WSREP_UUID_UNDEFINED),
    sst_seqno_          (WSREP_SEQNO_UNDEFINED),
    sst_mutex_          (),
    sst_cond_           (),
    sst_retry_sec_      (1),
    sst_received_       (false),
    gcache_             (config_, config_.get(BASE_DIR)),
    gcs_                (config_, gcache_, proto_max_, args->proto_ver,
                         args->node_name, args->node_incoming),
    service_thd_        (gcs_, gcache_),
    slave_pool_         (sizeof(TrxHandleSlave), 1024, "TrxHandleSlave"),
    as_                 (new GcsActionSource(slave_pool_, gcs_, *this, gcache_)),
    ist_receiver_       (config_, gcache_, slave_pool_,*this,args->node_address),
    ist_senders_        (gcache_),
    wsdb_               (),
    cert_               (config_, &service_thd_),
    pending_cert_queue_ (),
    local_monitor_      (),
    apply_monitor_      (),
    commit_monitor_     (),
    causal_read_timeout_(config_.get(Param::causal_read_timeout)),
    receivers_          (),
    replicated_         (),
    replicated_bytes_   (),
    keys_count_         (),
    keys_bytes_         (),
    data_bytes_         (),
    unrd_bytes_         (),
    local_commits_      (),
    local_rollbacks_    (),
    local_cert_failures_(),
    local_replays_      (),
    causal_reads_       (),
    preordered_id_      (),
    incoming_list_      (""),
    incoming_mutex_     (),
    wsrep_stats_        ()
{
    // @todo add guards (and perhaps actions)
    state_.add_transition(Transition(S_CLOSED,  S_DESTROYED));
    state_.add_transition(Transition(S_CLOSED,  S_CONNECTED));

    state_.add_transition(Transition(S_CONNECTED, S_CLOSED));
    state_.add_transition(Transition(S_CONNECTED, S_CONNECTED));
    state_.add_transition(Transition(S_CONNECTED, S_JOINING));
    // the following is possible only when bootstrapping new cluster
    // (trivial wsrep_cluster_address)
    state_.add_transition(Transition(S_CONNECTED, S_JOINED));
    // the following are possible on PC remerge
    state_.add_transition(Transition(S_CONNECTED, S_DONOR));
    state_.add_transition(Transition(S_CONNECTED, S_SYNCED));

    state_.add_transition(Transition(S_JOINING, S_CLOSED));
    // the following is possible if one non-prim conf follows another
    state_.add_transition(Transition(S_JOINING, S_CONNECTED));
    state_.add_transition(Transition(S_JOINING, S_JOINED));

    state_.add_transition(Transition(S_JOINED, S_CLOSED));
    state_.add_transition(Transition(S_JOINED, S_CONNECTED));
    state_.add_transition(Transition(S_JOINED, S_SYNCED));
    // the following is possible if one desync() immediately follows another
    state_.add_transition(Transition(S_JOINED, S_DONOR));

    state_.add_transition(Transition(S_SYNCED, S_CLOSED));
    state_.add_transition(Transition(S_SYNCED, S_CONNECTED));
    state_.add_transition(Transition(S_SYNCED, S_DONOR));

    state_.add_transition(Transition(S_DONOR, S_CLOSED));
    state_.add_transition(Transition(S_DONOR, S_CONNECTED));
    state_.add_transition(Transition(S_DONOR, S_JOINED));

    local_monitor_.set_initial_position(WSREP_UUID_UNDEFINED, 0);

    wsrep_uuid_t  uuid;
    wsrep_seqno_t seqno;

    st_.get (uuid, seqno, safe_to_bootstrap_);

    if (0 != args->state_id &&
        args->state_id->uuid != WSREP_UUID_UNDEFINED &&
        args->state_id->uuid == uuid                 &&
        seqno                == WSREP_SEQNO_UNDEFINED)
    {
        /* non-trivial recovery information provided on startup, and db is safe
         * so use recovered seqno value */
        seqno = args->state_id->seqno;
    }

    log_debug << "End state: " << uuid << ':' << seqno << " #################";

    cc_seqno_ = seqno; // is it needed here?

    set_initial_position(uuid, seqno);
    gcache_.seqno_reset(gu::GTID(uuid, seqno));
    // update gcache position to one supplied by app.

    build_stats_vars(wsrep_stats_);
}

void galera::ReplicatorSMM::start_closing()
{
    assert(closing_mutex_.locked());
    assert(state_() >= S_CONNECTED);
    if (!closing_)
    {
        closing_ = true;
        gcs_.close();
    }
}

void galera::ReplicatorSMM::shift_to_CLOSED()
{
    assert(closing_mutex_.locked());
    assert(closing_);

    state_.shift_to(S_CLOSED);

    /* Cleanup for re-opening. */
    uuid_ = WSREP_UUID_UNDEFINED;
    closing_ = false;
    if (st_.corrupt())
    {
        /* this is a synchronization hack to make sure all receivers are done
         * with their work and won't access cert module any more. The usual
         * monitor drain is not enough here. */
        while (receivers_() > 1) usleep(1000);

        // this should erase the memory of a pre-existing state.
        set_initial_position(WSREP_UUID_UNDEFINED, WSREP_SEQNO_UNDEFINED);
        cert_.assign_initial_position(gu::GTID(GU_UUID_NIL, -1),
                                      trx_params_.version_);
        sst_uuid_            = WSREP_UUID_UNDEFINED;
        sst_seqno_           = WSREP_SEQNO_UNDEFINED;
        cc_seqno_            = WSREP_SEQNO_UNDEFINED;
        cc_lowest_trx_seqno_ = WSREP_SEQNO_UNDEFINED;
        pause_seqno_         = WSREP_SEQNO_UNDEFINED;
    }

    closing_cond_.broadcast();
}

void galera::ReplicatorSMM::wait_for_CLOSED(gu::Lock& lock)
{
    assert(closing_mutex_.locked());
    assert(closing_);
    while (state_() > S_CLOSED) lock.wait(closing_cond_);
    assert(!closing_);
    assert(WSREP_UUID_UNDEFINED == uuid_);
}

galera::ReplicatorSMM::~ReplicatorSMM()
{
    log_info << "dtor state: " << state_();

    gu::Lock lock(closing_mutex_);

    switch (state_())
    {
    case S_CONNECTED:
    case S_JOINING:
    case S_JOINED:
    case S_SYNCED:
    case S_DONOR:
        start_closing();
        wait_for_CLOSED(lock);
        // fall through
    case S_CLOSED:
        ist_senders_.cancel();
        break;
    case S_DESTROYED:
        break;
    }

    delete as_;
}


wsrep_status_t galera::ReplicatorSMM::connect(const std::string& cluster_name,
                                              const std::string& cluster_url,
                                              const std::string& state_donor,
                                              bool  const        bootstrap)
{
    sst_donor_ = state_donor;
    service_thd_.reset();

    // make sure there was a proper initialization/cleanup
    assert(WSREP_UUID_UNDEFINED == uuid_);

    ssize_t err = 0;
    wsrep_status_t ret(WSREP_OK);
    wsrep_seqno_t const seqno(STATE_SEQNO());
    wsrep_uuid_t  const gcs_uuid(seqno < 0 ? WSREP_UUID_UNDEFINED :state_uuid_);
    gu::GTID      const inpos(gcs_uuid, seqno);

    log_info << "Setting GCS initial position to " << inpos;

    if ((bootstrap == true || cluster_url == "gcomm://")
        && safe_to_bootstrap_ == false)
    {
        log_error << "It may not be safe to bootstrap the cluster from this node. "
                  << "It was not the last one to leave the cluster and may "
                  << "not contain all the updates. To force cluster bootstrap "
                  << "with this node, edit the grastate.dat file manually and "
                  << "set safe_to_bootstrap to 1 .";
        ret = WSREP_NODE_FAIL;
    }

    if (ret == WSREP_OK && (err = gcs_.set_initial_position(inpos)) != 0)
    {
        log_error << "gcs init failed:" << strerror(-err);
        ret = WSREP_NODE_FAIL;
    }

    if (ret == WSREP_OK &&
        (err = gcs_.connect(cluster_name, cluster_url, bootstrap)) != 0)
    {
        log_error << "gcs connect failed: " << strerror(-err);
        ret = WSREP_NODE_FAIL;
    }

    if (ret == WSREP_OK)
    {
        state_.shift_to(S_CONNECTED);
    }

    return ret;
}


wsrep_status_t galera::ReplicatorSMM::close()
{
    gu::Lock lock(closing_mutex_);

    if (state_() > S_CLOSED)
    {
        start_closing();
        wait_for_CLOSED(lock);
    }

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::async_recv(void* recv_ctx)
{
    assert(recv_ctx != 0);

    if (state_() <= S_CLOSED)
    {
        log_error <<"async recv cannot start, provider in CLOSED state";
        return WSREP_FATAL;
    }

    ++receivers_;

    bool exit_loop(false);
    wsrep_status_t retval(WSREP_OK);

    while (WSREP_OK == retval && state_() > S_CLOSED)
    {
        ssize_t rc;

        while (gu_unlikely((rc = as_->process(recv_ctx, exit_loop))
                           == -ECANCELED))
        {
            recv_IST(recv_ctx);
            // hack: prevent fast looping until ist controlling thread
            // resumes gcs prosessing
            usleep(10000);
        }

        if (gu_unlikely(rc <= 0))
        {
            retval = WSREP_CONN_FAIL;
        }
        else if (gu_unlikely(exit_loop == true))
        {
            assert(WSREP_OK == retval);

            if (receivers_.sub_and_fetch(1) > 0)
            {
                log_info << "Slave thread exiting on request.";
                break;
            }

            ++receivers_;
            log_warn << "Refusing exit for the last slave thread.";
        }
    }

    /* exiting loop already did proper checks */
    if (!exit_loop && receivers_.sub_and_fetch(1) == 0)
    {
        gu::Lock lock(closing_mutex_);

        if (state_() > S_CLOSED && !closing_)
        {
            assert(WSREP_CONN_FAIL == retval);
            /* Last recv thread exiting due to error but replicator is not
             * closed. We need to at least gracefully leave the cluster.*/

            log_warn << "Broken shutdown sequence, provider state: "
                     << state_() << ", retval: " << retval;
            assert (0);

            /* avoid abort in production */
            start_closing();

            // Generate zero view before exit to notify application
            gcs_act_cchange const cc;
            wsrep_uuid_t tmp(uuid_);
            wsrep_view_info_t* const err_view
                (galera_view_info_create(cc, -1, tmp));
            view_cb_(app_ctx_, recv_ctx, err_view, 0, 0);
            free(err_view);

            shift_to_CLOSED();
        }
    }

    log_debug << "Slave thread exit. Return code: " << retval;

    return retval;
}

void galera::ReplicatorSMM::apply_trx(void* recv_ctx, TrxHandleSlave& ts)
{
    assert(ts.global_seqno() > 0);
    if (!ts.skip_event())
    {
        assert(ts.trx_id() != uint64_t(-1) || ts.is_toi());
        assert(ts.certified() /*Repl*/ || ts.preordered() /*IST*/);
        assert(ts.local() == false ||
               (ts.flags() & TrxHandle::F_ROLLBACK));
    }

    ApplyException ae;

    ApplyOrder ao(ts);
    CommitOrder co(ts, co_mode_);

    bool const applying(ts.must_enter_am());

    if (gu_likely(TrxHandle::S_ABORTING != ts.state()))
        ts.set_state((ts.flags() & TrxHandle::F_ROLLBACK) /* expl. rollback */ ?
                     TrxHandle::S_ABORTING : TrxHandle::S_APPLYING);

    if (gu_likely(applying))
    {
        gu_trace(apply_monitor_.enter(ao));
    }

    wsrep_trx_meta_t meta = { { state_uuid_,    ts.global_seqno() },
                              { ts.source_id(), ts.trx_id(), ts.conn_id() },
                              ts.depends_seqno() };

    if (ts.is_toi())
    {
        log_debug << "Executing TO isolated action: " << ts;
        st_.mark_unsafe();
    }

    wsrep_bool_t exit_loop(false);

    try { gu_trace(ts.apply(recv_ctx, apply_cb_, meta, exit_loop)); }
    catch (ApplyException& e)
    {
        assert(0 != e.status());
        assert(NULL != e.data() || 0 == e.data_len());
        assert(0 != e.data_len() || NULL == e.data());

        if (!st_.corrupt()) mark_corrupt_and_close();
    }
    /* at this point any other exception is fatal, not catching anything else.*/

    if (ts.local() == false)
    {
        GU_DBUG_SYNC_WAIT("after_commit_slave_sync");
    }

    wsrep_seqno_t safe_to_discard(WSREP_SEQNO_UNDEFINED);
    if (gu_likely(applying))
    {
        /* For now need to keep it inside apply monitor to ensure all processing
         * ends by the time monitors are drained because of potential gcache
         * cleanup (and loss of the writeset buffer). Perhaps unordered monitor
         * is needed here. */
        ts.unordered(recv_ctx, unordered_cb_);

        safe_to_discard = cert_.set_trx_committed(ts);

        apply_monitor_.leave(ao);
    }

    if (ts.is_toi())
    {
        log_debug << "Done executing TO isolated action: "
                  << ts.global_seqno();
        st_.mark_safe();
    }

    if (gu_likely(ts.local_seqno() != -1))
    {
        // trx with local seqno -1 originates from IST (or other source not gcs)
        report_last_committed(safe_to_discard);
    }

    ts.set_exit_loop(exit_loop);
}


wsrep_status_t galera::ReplicatorSMM::send(TrxHandleMaster& trx,
                                           wsrep_trx_meta_t* meta)
{
    assert(trx.locked());
    if (state_() < S_JOINED) return WSREP_TRX_FAIL;

    // SR rollback
    const bool rollback(trx.flags() & TrxHandle::F_ROLLBACK);

    if (rollback)
    {
        assert(trx.state() == TrxHandle::S_ABORTING);
        assert((trx.flags() & TrxHandle::F_BEGIN) == 0);
        TrxHandleSlavePtr ts(TrxHandleSlave::New(true, slave_pool_),
                             TrxHandleSlaveDeleter());
        ts->set_global_seqno(0);
        trx.add_replicated(ts);
    }

    WriteSetNG::GatherVector actv;

    size_t act_size = trx.gather(actv);

    ssize_t rcode(0);
    do
    {
        const bool scheduled(!rollback);

        if (scheduled)
        {
            const ssize_t gcs_handle(gcs_.schedule());

            if (gu_unlikely(gcs_handle < 0))
            {
                log_debug << "gcs schedule " << strerror(-gcs_handle);
                rcode = gcs_handle;
                goto out;
            }
            trx.set_gcs_handle(gcs_handle);
        }

        trx.finalize(last_committed());
        trx.unlock();
        // On rollback fragment, we instruct sendv to use gcs_sm_grab()
        // to avoid the scenario where trx is BF aborted but can't send
        // ROLLBACK fragment due to flow control, which results in
        // deadlock.
        // Otherwise sendv call was scheduled above, and we instruct
        // the call to use regular gcs_sm_enter()
        const bool grab(rollback);
        rcode = gcs_.sendv(actv, act_size,
                           GCS_ACT_WRITESET,
                           scheduled, grab);
        GU_DBUG_SYNC_WAIT("after_send_sync");
        trx.lock();
    }
    // TODO: Break loop after some timeout
    while (rcode == -EAGAIN && (usleep(1000), true));

    trx.set_gcs_handle(-1);

out:

    if (rcode <= 0)
    {
        log_debug << "ReplicatorSMM::send failed: " << -rcode;
    }

    return (rcode > 0 ? WSREP_OK : WSREP_TRX_FAIL);
}


wsrep_status_t galera::ReplicatorSMM::replicate(TrxHandleMaster& trx,
                                                wsrep_trx_meta_t* meta)
{
    assert(trx.locked());

    assert(trx.state() == TrxHandle::S_EXECUTING ||
           trx.state() == TrxHandle::S_MUST_ABORT);

    if (state_() < S_JOINED || trx.state() == TrxHandle::S_MUST_ABORT)
    {
    must_abort:
        if (trx.state() == TrxHandle::S_EXECUTING ||
            trx.state() == TrxHandle::S_REPLICATING)
            trx.set_state(TrxHandle::S_MUST_ABORT);

        trx.set_state(TrxHandle::S_ABORTING);

        if (trx.ts() != 0)
        {
            assert(trx.ts()->state() == TrxHandle::S_COMMITTED);
            trx.reset_ts();
        }

        return (st_.corrupt() ? WSREP_NODE_FAIL : WSREP_CONN_FAIL);
    }

    WriteSetNG::GatherVector actv;

    gcs_action act;
    act.type = GCS_ACT_WRITESET;
#ifndef NDEBUG
    act.seqno_g = GCS_SEQNO_ILL;
#endif

    act.buf  = NULL;
    act.size = trx.gather(actv);
    trx.set_state(TrxHandle::S_REPLICATING);

    ssize_t rcode(-1);

    do
    {
        assert(act.seqno_g == GCS_SEQNO_ILL);

        const ssize_t gcs_handle(gcs_.schedule());

        if (gu_unlikely(gcs_handle < 0))
        {
            log_debug << "gcs schedule " << strerror(-gcs_handle);
            goto must_abort;
        }

        trx.set_gcs_handle(gcs_handle);

        trx.finalize(last_committed());
        trx.unlock();
        assert (act.buf == NULL); // just a sanity check
        rcode = gcs_.replv(actv, act, true);

        GU_DBUG_SYNC_WAIT("after_replicate_sync")
        trx.lock();
    }
    while (rcode == -EAGAIN && trx.state() != TrxHandle::S_MUST_ABORT &&
           (usleep(1000), true));

    trx.set_gcs_handle(-1);

    if (rcode < 0)
    {
        if (rcode != -EINTR)
        {
            log_debug << "gcs_repl() failed with " << strerror(-rcode)
                      << " for trx " << trx;
        }

        assert(rcode != -EINTR || trx.state() == TrxHandle::S_MUST_ABORT);
        assert(act.seqno_l == GCS_SEQNO_ILL && act.seqno_g == GCS_SEQNO_ILL);
        assert(NULL == act.buf);

        if (trx.state() != TrxHandle::S_MUST_ABORT)
        {
            trx.set_state(TrxHandle::S_MUST_ABORT);
        }

        goto must_abort;
    }

    assert(act.buf != NULL);
    assert(act.size == rcode);
    assert(act.seqno_l > 0);
    assert(act.seqno_g > 0);

    TrxHandleSlavePtr ts(TrxHandleSlave::New(true, slave_pool_),
                         TrxHandleSlaveDeleter());

    gu_trace(ts->unserialize<true>(act));
    ts->set_local(true);

    ts->update_stats(keys_count_, keys_bytes_, data_bytes_, unrd_bytes_);

    trx.add_replicated(ts);

    ++replicated_;
    replicated_bytes_ += rcode;

    assert(trx.source_id() == ts->source_id());
    assert(trx.conn_id()   == ts->conn_id());
    assert(trx.trx_id()    == ts->trx_id());

    assert(ts->global_seqno() == act.seqno_g);
    assert(ts->last_seen_seqno() >= 0);

    assert(trx.ts() == ts);

    wsrep_status_t retval(WSREP_TRX_FAIL);

    // ROLLBACK event shortcut to avoid blocking in monitors or
    // getting BF aborted inside provider
    if (gu_unlikely(trx.flags() & TrxHandle::F_ROLLBACK))
    {
        assert(ts->depends_seqno() > 0); // must be set at unserialization
        ts->cert_bypass(true);
        ts->mark_certified();
        gcache_.seqno_assign(ts->action().first, ts->global_seqno(),
                             GCS_ACT_WRITESET, false);
        cancel_monitors<true>(*ts);

        trx.set_state(TrxHandle::S_MUST_ABORT);
        trx.set_state(TrxHandle::S_ABORTING);
        ts->set_state(TrxHandle::S_ABORTING); // to pass asserts in post_rollback

        goto out;
    }

    if (gu_unlikely(trx.state() == TrxHandle::S_MUST_ABORT))
    {
        retval = cert_for_aborted(ts);

        if (retval != WSREP_BF_ABORT)
        {
            cancel_monitors<true>(*ts);

            assert(trx.state() == TrxHandle::S_MUST_ABORT);
            trx.set_state(TrxHandle::S_ABORTING);
            assert(ts->is_dummy());
            assert(WSREP_OK != retval);
        }
        else
        {
            // If the transaction was committing, it must replay.
            if (ts->flags() & TrxHandle::F_COMMIT)
            {
                trx.set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
            }
            else
            {
//remove                trx.reset_ts();
                pending_cert_queue_.push(ts);

                LocalOrder lo(*ts);
                local_monitor_.self_cancel(lo);
                ApplyOrder ao(*ts);
                apply_monitor_.self_cancel(ao);
#if 0 //remove
                if (co_mode_ != CommitOrder::BYPASS)
                {
                    CommitOrder co(*ts, co_mode_);
                    commit_monitor_.self_cancel(co);
                }
#endif
                ts->set_state(TrxHandle::S_ABORTING);
                trx.set_state(TrxHandle::S_ABORTING);

                retval = WSREP_TRX_FAIL;
//remove                assert(NULL == meta ||
//                       meta->gtid.seqno == WSREP_SEQNO_UNDEFINED);
//                return retval;
            }
        }
    }
    else
    {
        assert(trx.state() == TrxHandle::S_REPLICATING);
        retval = WSREP_OK;
    }

out:
    assert(trx.state() != TrxHandle::S_MUST_ABORT);
    assert(ts->global_seqno() >  0);
    assert(ts->global_seqno() == act.seqno_g);

    if (meta != 0) // whatever the retval, we must update GTID in meta
    {
        meta->gtid.uuid  = state_uuid_;
        meta->gtid.seqno = ts->global_seqno();
        meta->depends_on = ts->depends_seqno();
    }

    return retval;
}

wsrep_status_t
galera::ReplicatorSMM::abort_trx(TrxHandle* trx, wsrep_seqno_t bf_seqno,
                                 wsrep_seqno_t* victim_seqno)
{
    assert(trx.local() == true);
    assert(trx.locked());

    const TrxHandleSlavePtr ts(trx.ts());

    if (ts)
    {
        log_debug << "aborting ts  " << *ts;
        assert(ts->global_seqno() != WSREP_SEQNO_UNDEFINED);
        if (ts->global_seqno() < bf_seqno &&
            (ts->flags() & TrxHandle::F_COMMIT))
        {
            log_debug << "seqno " << bf_seqno
                      << " trying to abort seqno " << ts->global_seqno();
            *victim_seqno = ts->global_seqno();
            return WSREP_NOT_ALLOWED;
        }
    }

    wsrep_status_t retval(WSREP_OK);
    switch (trx->state())
    {
    case TrxHandle::S_MUST_ABORT:
    case TrxHandle::S_ABORTING:
        // victim trx was already BF aborted or it failed certification
        retval = WSREP_NOT_ALLOWED;
        break;
    case TrxHandle::S_EXECUTING:
        trx.set_state(TrxHandle::S_MUST_ABORT);
        break;
    case TrxHandle::S_REPLICATING:
    {
        // @note: it is important to place set_state() into beginning of
        // every case, because state must be changed AFTER switch() and
        // BEFORE entering monitors or taking any other action.
        trx.set_state(TrxHandle::S_MUST_ABORT);
        int rc;
        if (trx.gcs_handle() > 0 &&
            ((rc = gcs_.interrupt(trx.gcs_handle()))) != 0)
        {
            log_debug << "gcs_interrupt(): handle "
                      << trx.gcs_handle()
                      << " trx id " << trx.trx_id()
                      << ": " << strerror(-rc);
        }
        break;
    }
    case TrxHandle::S_CERTIFYING:
    {
        // trx is waiting in local monitor
        assert(ts);
        assert(ts->global_seqno() > 0);
        log_debug << "aborting ts: " << *ts << "; BF seqno: " << bf_seqno
                  << "; local position: " << local_monitor_.last_left();
        trx.set_state(TrxHandle::S_MUST_ABORT);
        LocalOrder lo(*ts);
        local_monitor_.interrupt(lo);
        break;
    }
    case TrxHandle::S_APPLYING:
    {
        // trx is waiting in apply monitor
        assert(ts);
        assert(ts->global_seqno() > 0);
        log_debug << "aborting ts: " << *ts << "; BF seqno: " << bf_seqno
                  << "; apply window: " << apply_monitor_.last_left() << " - "
                  << apply_monitor_.last_entered();
        trx.set_state(TrxHandle::S_MUST_ABORT);
        ApplyOrder ao(*ts);
        apply_monitor_.interrupt(ao);
        break;
    }
    case TrxHandle::S_COMMITTING:
    {
        // Trx is waiting in commit monitor
        assert(ts);
        assert(ts->global_seqno() > 0);
        log_debug << "aborting ts: " << *ts << "; BF seqno: " << bf_seqno
                  << "; commit position: " << last_committed();
        if (co_mode_ != CommitOrder::BYPASS)
        {
            CommitOrder co(*ts, co_mode_);
            bool const interrupted(commit_monitor_.interrupt(co));
            if (interrupted)
            {
                trx->set_state(TrxHandle::S_MUST_ABORT);
            }
            else
            {
                retval = WSREP_NOT_ALLOWED;
            }
        }
        break;
    }
    case TrxHandle::S_COMMITTED:
        assert(ts);
        assert(ts->global_seqno() > 0);
        if (ts->global_seqno() < bf_seqno &&
            (ts->flags() & TrxHandle::F_COMMIT))
        {
            retval = WSREP_NOT_ALLOWED;
        }
        else
        {
            retval = WSREP_OK;
        }
        break;
    case TrxHandle::S_ROLLING_BACK:
        log_error << "Attempt to enter commit monitor while holding "
            "locks in rollback by " << trx;
        // fallthrough
    default:
        log_warn << "invalid state " << trx.state()
                 << " in abort_trx for trx"
                 << trx;
        assert(0);
    }
    if (retval == WSREP_OK || retval == WSREP_NOT_ALLOWED)
    {
        *victim_seqno = trx->global_seqno();
    }
    return retval;
}


wsrep_status_t galera::ReplicatorSMM::certify(TrxHandleMaster&  trx,
                                              wsrep_trx_meta_t* meta)
{
    assert(trx.state() == TrxHandle::S_REPLICATING);

    TrxHandleSlavePtr ts(trx.ts());
    assert(ts->state() == TrxHandle::S_REPLICATING);

    // Rollback should complete with post_rollback
    assert((ts->flags() & TrxHandle::F_ROLLBACK) == 0);

    assert(ts->local_seqno()  > 0);
    assert(ts->global_seqno() > 0);
    assert(ts->last_seen_seqno() >= 0);
    assert(ts->depends_seqno() >= -1);

    if (meta != 0)
    {
        assert(meta->gtid.uuid  == state_uuid_);
        assert(meta->gtid.seqno == ts->global_seqno());
        assert(meta->depends_on == ts->depends_seqno());
    }
    // State should not be checked here: If trx has been replicated,
    // it has to be certified and potentially applied. #528
    // if (state_() < S_JOINED) return WSREP_TRX_FAIL;

    wsrep_status_t retval(cert_and_catch(&trx, ts));

    assert((ts->flags() & TrxHandle::F_ROLLBACK) == 0 ||
           trx.state() == TrxHandle::S_ABORTING);

    if (gu_unlikely(retval != WSREP_OK))
    {
        switch(retval)
        {
        case WSREP_BF_ABORT:
            assert(ts->depends_seqno() >= 0);
            assert(trx.state() == TrxHandle::S_MUST_CERT_AND_REPLAY ||
                   trx.state() == TrxHandle::S_MUST_REPLAY_AM);
            break;
        case WSREP_TRX_FAIL:
            assert(trx.state() == TrxHandle::S_ABORTING);
            break;
        default:
            assert(0);
        }

        return retval;
    }

    assert(ts->global_seqno() > STATE_SEQNO());
    assert(ts->depends_seqno() >= 0);

    trx.set_state(TrxHandle::S_APPLYING);

    ApplyOrder  ao(*ts);
    bool interrupted(false);

    try
    {
        trx.unlock();
        GU_DBUG_SYNC_WAIT("before_certify_apply_monitor_enter");
        gu_trace(apply_monitor_.enter(ao));
        GU_DBUG_SYNC_WAIT("after_certify_apply_monitor_enter");
        trx.lock();
        assert(trx.state() == TrxHandle::S_APPLYING ||
               trx.state() == TrxHandle::S_MUST_ABORT);
    }
    catch (gu::Exception& e)
    {
        trx.lock();
        if (e.get_errno() == EINTR)
        {
            interrupted = true;
        }
        else throw;
    }

    if (gu_unlikely(interrupted || trx.state() == TrxHandle::S_MUST_ABORT))
    {
        assert(trx.state() == TrxHandle::S_MUST_ABORT);
        if (ts->flags() & TrxHandle::F_COMMIT)
        {
            trx.set_state(TrxHandle::S_MUST_REPLAY_AM);
        }
        else
        {
            if (interrupted == true)
            {
                apply_monitor_.self_cancel(ao);
            }
            else if (apply_monitor_.entered(ao))
            {
                apply_monitor_.leave(ao);
            }

            ts->set_state(TrxHandle::S_ABORTING);
            trx.set_state(TrxHandle::S_ABORTING);
        }
        retval = WSREP_BF_ABORT;
    }
    else
    {
        assert(apply_monitor_.entered(ao));
        ts->set_state(TrxHandle::S_APPLYING);
    }

    assert(trx.state() != TrxHandle::S_MUST_ABORT);

    assert((retval == WSREP_OK && (trx.state() == TrxHandle::S_APPLYING ||
                                   trx.state() == TrxHandle::S_EXECUTING))
           ||
           (retval == WSREP_BF_ABORT && (
               trx.state() == TrxHandle::S_MUST_REPLAY_AM ||
               trx.state() == TrxHandle::S_MUST_REPLAY_CM ||
               trx.state() == TrxHandle::S_ABORTING))
           ||
           (retval == WSREP_TRX_FAIL && trx.state() == TrxHandle::S_ABORTING)
        );

    if (meta) meta->depends_on = ts->depends_seqno();

    return retval;
}


wsrep_status_t galera::ReplicatorSMM::replay_trx(TrxHandleMaster& trx,
                                                 void* const      trx_ctx)
{
    TrxHandleSlavePtr tsp(trx.ts());
    assert(tsp);
    TrxHandleSlave& ts(*tsp);

    assert(ts.global_seqno() > STATE_SEQNO());

    log_debug << "replay trx: " << trx << " ts: " << ts;

    if (trx.state() == TrxHandle::S_MUST_ABORT)
    {
        /* Aborted after certify() returned (meaning apply monitor entered) */
#ifndef NDEBUG
        ApplyOrder ao(ts);
        assert(apply_monitor_.entered(ao));
#endif
        trx.set_state(TrxHandle::S_MUST_REPLAY_CM);
    }

    assert(trx.state() == TrxHandle::S_MUST_CERT_AND_REPLAY ||
           trx.state() == TrxHandle::S_MUST_REPLAY_AM       ||
           trx.state() == TrxHandle::S_MUST_REPLAY_CM);
    assert(trx.trx_id() != static_cast<wsrep_trx_id_t>(-1));

    wsrep_status_t retval(WSREP_OK);

    // Note: We set submit NULL trx pointer below to avoid
    // interrupting replaying in any monitor during replay.

    switch (trx.state())
    {
    case TrxHandle::S_MUST_CERT_AND_REPLAY:
        retval = cert_and_catch(&trx, tsp);
        if (retval != WSREP_OK)
        {
            assert(retval == WSREP_TRX_FAIL);
            assert(trx.state() == TrxHandle::S_ABORTING);
            // apply monitor is self canceled in cert
            break;
        }
        trx.set_state(TrxHandle::S_MUST_REPLAY_AM);
        ts.set_state(TrxHandle::S_APPLYING);
        // fall through
    case TrxHandle::S_MUST_REPLAY_AM:
    {
        // safety measure to make sure that all preceding trxs finish before
        // replaying
        assert(ts.state() == TrxHandle::S_APPLYING);
        wsrep_seqno_t const ds(ts.depends_seqno());
        ts.set_depends_seqno(ts.global_seqno() - 1);
        ApplyOrder ao(ts);
        if (apply_monitor_.entered(ao) == false)
        {
            gu_trace(apply_monitor_.enter(ao));
        }
        // restore dependency info
        ts.set_depends_seqno(WSREP_SEQNO_UNDEFINED);
        ts.set_depends_seqno(ds);
        trx.set_state(TrxHandle::S_MUST_REPLAY_CM);
    }
    // fall through
    case TrxHandle::S_MUST_REPLAY_CM:
        if (co_mode_ != CommitOrder::BYPASS)
        {
            CommitOrder co(ts, co_mode_);
            if (commit_monitor_.entered(co) == false)
            {
                gu_trace(commit_monitor_.enter(co));
            }
        }
        trx.set_state(TrxHandle::S_MUST_REPLAY);
        // fall through
    case TrxHandle::S_MUST_REPLAY:
        ++local_replays_;

        trx.set_state(TrxHandle::S_REPLAYING);
        try
        {
            // Only committing transactions should be replayed
            assert(ts.flags() & TrxHandle::F_COMMIT);

            wsrep_trx_meta_t meta = {{ state_uuid_,    ts.global_seqno() },
                                     { ts.source_id(), ts.trx_id(),
                                       ts.conn_id()                      },
                                     ts.depends_seqno()};

            /* this is essential to choose correct commit_order_enter_xxx() */
            ts.set_state(TrxHandle::S_REPLAYING);

            /* failure to replay own trx is certainly a sign of inconsistency,
             * not trying to catch anything here */
            assert(trx.owned());
            bool unused(false);
            gu_trace(ts.apply(trx_ctx, apply_cb_, meta, unused));
            assert(false == unused);
            log_debug << "replayed " << ts.global_seqno();
            assert(ts.state() == TrxHandle::S_COMMITTED);
            trx.set_state(ts.state());
        }
        catch (gu::Exception& e)
        {
            mark_corrupt_and_close();
            throw;
        }

        // apply, commit monitors are released in post commit
        return WSREP_OK;
    default:
        assert(0);
        gu_throw_fatal << "Invalid state in replay for trx " << trx;
    }

    log_debug << "replaying failed for trx " << trx;
    assert(trx.state() == TrxHandle::S_ABORTING);

    return retval;
}

static void
dump_buf(std::ostream& os, const void* const buf, size_t const buf_len)
{
    std::ios_base::fmtflags const saved_flags(os.flags());
    char                    const saved_fill (os.fill('0'));

    os << std::oct;

    const char* const str(static_cast<const char*>(buf));
    for (size_t i(0); i < buf_len; ++i)
    {
        char const c(str[i]);

        if ('\0' == c) break;

        try
        {
            if (isprint(c) || isspace(c))
            {
                os.put(c);
            }
            else
            {
                os << '\\' << std::setw(2) << int(c);
            }
        }
        catch (std::ios_base::failure& f)
        {
            log_warn << "Failed to dump " << i << "th byte: " << f.what();
            break;
        }
    }

    os.flags(saved_flags);
    os.fill (saved_fill);
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_enter_local(TrxHandleMaster& trx)
{
    assert(trx.local());
    assert(trx.ts() && trx.ts()->global_seqno() > 0);
    assert(trx.locked());

    assert(trx.state() == TrxHandle::S_APPLYING  ||
           trx.state() == TrxHandle::S_ABORTING);

    TrxHandle::State const next_state
        (trx.state() == TrxHandle::S_ABORTING ?
         TrxHandle::S_ROLLING_BACK : TrxHandle::S_COMMITTING);

    trx.set_state(next_state);

    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        TrxHandleSlavePtr tsp(trx.ts());
        TrxHandleSlave& ts(*tsp);
        ts.set_state(trx.state());

        CommitOrder co(ts, co_mode_);
        assert(!commit_monitor_.entered(co));

        try
        {
            trx.unlock();
            GU_DBUG_SYNC_WAIT("before_local_commit_monitor_enter");
            gu_trace(commit_monitor_.enter(co));
            trx.lock();
            assert(trx.state() == TrxHandle::S_COMMITTING ||
                   trx.state() == TrxHandle::S_ROLLING_BACK);
        }
        catch (gu::Exception& e)
        {
            assert(next_state != TrxHandle::S_ROLLING_BACK);
            trx.lock();
            if (e.get_errno() == EINTR)
            {
                assert(trx.state() == TrxHandle::S_MUST_ABORT);

                if (ts.flags() & TrxHandle::F_COMMIT)
                {
                    trx.set_state(TrxHandle::S_MUST_REPLAY_CM);
                    return WSREP_BF_ABORT;
                }
                else
                {
                    ApplyOrder ao(ts);
                    apply_monitor_.leave(ao);

                    ts.set_state(TrxHandle::S_ABORTING);
                    trx.set_state(TrxHandle::S_ABORTING);
                    return WSREP_TRX_FAIL;
                }
            }
            else throw;
        }
        assert(ts.global_seqno() > STATE_SEQNO());
    }
    assert(trx.locked());

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_ROLLING_BACK);

    return WSREP_OK;
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_enter_remote(TrxHandleSlave& trx)
{
    assert(trx.local() == false ||
           trx.state() == TrxHandle::S_REPLAYING ||
           trx.flags() & TrxHandle::F_ROLLBACK /* explicit rollback */);
    assert(trx.global_seqno() > 0);

    assert(trx.state() == TrxHandle::S_APPLYING  ||
           trx.state() == TrxHandle::S_REPLAYING ||
           trx.state() == TrxHandle::S_ABORTING);

#ifndef NDEBUG
    if (trx.state() == TrxHandle::S_REPLAYING)
    {
        assert(trx.local());
        assert((trx.flags() & TrxHandle::F_ROLLBACK) == 0);
    }
#endif /* NDEBUG */

    CommitOrder co(trx, co_mode_);
    assert(!commit_monitor_.entered(co) || trx.state() ==TrxHandle::S_REPLAYING);
    assert(trx.state() ==TrxHandle::S_REPLAYING || !commit_monitor_.entered(co));

    if (trx.state() != TrxHandle::S_REPLAYING)
        trx.set_state(trx.state() == TrxHandle::S_ABORTING ?
                      TrxHandle::S_ROLLING_BACK : TrxHandle::S_COMMITTING);

    if (gu_likely(co_mode_ != CommitOrder::BYPASS &&
                  trx.state() != TrxHandle::S_REPLAYING))
    {
        gu_trace(commit_monitor_.enter(co));
    }

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_REPLAYING  ||
           trx.state() == TrxHandle::S_ROLLING_BACK);

    return WSREP_OK;
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_leave(TrxHandleSlave&          trx,
                                          const wsrep_buf_t* const error)
{
    if (trx.state() == TrxHandle::S_MUST_ABORT)
    {
        // This is possible in case of ALG: BF applier BF aborts
        // trx that has already grabbed commit monitor and is committing.
        // However, this should be acceptable assuming that commit
        // operation does not reserve any more resources and is able
        // to release already reserved resources.
        log_debug << "trx was BF aborted during commit: " << trx;
        // manipulate state to avoid crash
        trx.set_state(TrxHandle::S_MUST_REPLAY);
        trx.set_state(TrxHandle::S_REPLAYING);
    }

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_REPLAYING  ||
           trx.state() == TrxHandle::S_ABORTING  ||
           trx.state() == TrxHandle::S_ROLLING_BACK);
#ifndef NDEBUG
    {
        CommitOrder co(trx, co_mode_);
        assert(co_mode_ != CommitOrder::BYPASS || commit_monitor_.entered(co));
    }
#endif

    TrxHandle::State end_state(trx.state() == TrxHandle::S_ROLLING_BACK ?
                               TrxHandle::S_ROLLED_BACK :TrxHandle::S_COMMITTED);
    wsrep_status_t retval(WSREP_OK);

    if (gu_unlikely(error != NULL && error->ptr != NULL))
    {
        assert(error->len > 0);

        std::ostringstream os;

        os << "Failed to apply app action, seqno: " << trx.global_seqno()
           << ", error: ";

        dump_buf(os, error->ptr, error->len);

        if (!trx.is_toi())
        {
            if (!st_.corrupt()) mark_corrupt_and_close();

            end_state = TrxHandle::S_ROLLED_BACK;
            retval    = WSREP_NODE_FAIL;
        }
    }

    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        CommitOrder co(trx, co_mode_);
        commit_monitor_.leave(co);
    }

    trx.set_state(end_state);
    /* master state will be set upon release */

    return retval;
}

wsrep_status_t galera::ReplicatorSMM::release_commit(TrxHandleMaster& trx)
{
    TrxHandleSlavePtr tsp(trx.ts());
    assert(tsp);
    TrxHandleSlave& ts(*tsp);

#ifndef NDEBUG
    {
        CommitOrder co(ts, co_mode_);
        assert(co_mode_ == CommitOrder::BYPASS ||
               commit_monitor_.entered(co) == false);
    }
#endif

    log_debug << "release_commit() for trx: " << trx << " ts: " << ts;

    assert((ts.flags() & TrxHandle::F_ROLLBACK) == 0);
    assert(ts.local_seqno() > 0 && ts.global_seqno() > 0);
    assert(ts.state() == TrxHandle::S_COMMITTED);
    assert(trx.state() == TrxHandle::S_COMMITTED);

    wsrep_seqno_t const safe_to_discard(cert_.set_trx_committed(ts));

    ApplyOrder ao(ts);
    apply_monitor_.leave(ao);

    if ((ts.flags() & TrxHandle::F_COMMIT) == 0)
    {
        // continue streaming
        trx.set_state(TrxHandle::S_EXECUTING);
    }

    trx.reset_ts();

    ++local_commits_;

    report_last_committed(safe_to_discard);

    return WSREP_OK;
}

#if 0 //remove
//<<<<<<< HEAD
srep_status_t galera::ReplicatorSMM::post_rollback(TrxHandleMaster* trx)
{
    // * Cert failure or BF abort while inside pre_commit() call or
    // * BF abort between pre_commit() and pre_rollback() call
    assert(trx->state() == TrxHandle::S_EXECUTING ||
           trx->state() == TrxHandle::S_ABORTING  ||
           trx->state() == TrxHandle::S_MUST_ABORT);

    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        trx->set_state(TrxHandle::S_ABORTING);
    }

    TrxHandleSlavePtr ts(trx->ts());

    if (ts)
    {
        assert(ts->global_seqno() > 0); // BF'ed
        assert(trx->state() == TrxHandle::S_ABORTING);
        // We shold not care about ts state here, ts may have
        // been replicated succesfully and the transaction
        // has been BF aborted after ts has been applied.
        // assert(ts->state()  == TrxHandle::S_ABORTING);

        // There are two ways we can end up here:
        // 1) writeset must be skipped/trx rolled back
        // 2) trx passed certification and must commit,
        //    but was BF aborted and must replay
        assert((ts->flags() & TrxHandle::F_ROLLBACK) ||
               (ts->depends_seqno() >= 0));

        if (ts->pa_unsafe())
        {
            ApplyOrder ao(*ts);
            apply_monitor_.enter(ao);
        }

        if (co_mode_ != CommitOrder::BYPASS)
        {
            CommitOrder co(*ts, co_mode_);
            commit_monitor_.enter(co);
        }
    }

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_REPLAYING  ||
           trx.state() == TrxHandle::S_ROLLING_BACK);

    return WSREP_OK;
}
//=======
#endif //0


wsrep_status_t galera::ReplicatorSMM::release_rollback(TrxHandleMaster& trx)
{
    assert(trx.locked());
#if 0 //remove
//<<<<<<< HEAD
    // Release monitors if ts was not committed. We may enter here
    // with ts->state() == TrxHandle::S_COMMITTED if transaction
    // replicated a fragment succesfully and then voluntarily rolled
    // back by sending async rollback event via ReplicatorSMM::send()
    if (ts && ts->state() != TrxHandle::S_COMMITTED)
    {
        log_debug << "release_rollback() trx: " << *trx << ", ts: " << *ts;
        assert(ts->global_seqno() > 0); // BF'ed
        assert(trx->state() == TrxHandle::S_ABORTING);
        // We should not care about ts state here, ts may have
        // been replicated succesfully and the transaction
        // has been BF aborted after ts has been applied.
        // assert(ts->state()  == TrxHandle::S_ABORTING);

        log_debug << "Master rolled back " << ts->global_seqno();

        ApplyOrder ao(*ts);
        if (ts->pa_unsafe()) /* apply_monitor_ was entered */
        {
            assert(apply_monitor_.entered(ao));
            apply_monitor_.leave(ao);
        }
        else
        {
            assert(!apply_monitor_.entered(ao));
        }

        if (co_mode_ != CommitOrder::BYPASS)
        {
            CommitOrder co(*ts, co_mode_);
            assert(commit_monitor_.entered(co));
            commit_monitor_.leave(co);
        }

        ts->set_state(TrxHandle::S_ROLLED_BACK);

        report_last_committed(cert_.set_trx_committed(*ts));
    }
    else
    {
        log_debug << "release_rollback() trx: " << *trx;
    }

    trx->set_state(TrxHandle::S_ROLLED_BACK);

//=======
#endif //0

    if (trx.state() == TrxHandle::S_MUST_ABORT) // BF abort before replicaiton
        trx.set_state(TrxHandle::S_ABORTING);

    if (trx.state() == TrxHandle::S_ABORTING ||
        trx.state() == TrxHandle::S_EXECUTING)
        trx.set_state(TrxHandle::S_ROLLED_BACK);

    assert(trx.state() == TrxHandle::S_ROLLED_BACK);

    log_debug << "release_rollback() trx: " << trx;

    TrxHandleSlavePtr tsp(trx.ts());
    if (tsp)
    {
        TrxHandleSlave& ts(*tsp);

        log_info << "release_rollback() trx: " << trx << ", ts: " << ts;

#ifndef NDEBUG
        {
//remove            assert(ts.state() == TrxHandle::S_ROLLED_BACK);

            CommitOrder co(ts, co_mode_);
            assert(commit_monitor_.entered(co) == false);
        }
#endif /* NDEBUG */

        if (ts.global_seqno() > 0)
        {
            ApplyOrder ao(ts);
            if (apply_monitor_.last_left() < ts.global_seqno() &&
                !(apply_monitor_.entered(ao) || apply_monitor_.finished(ao)))
            {
                apply_monitor_.enter(ao);
            }
            if (apply_monitor_.entered(ao))
            {
                wsrep_seqno_t const safe_to_discard(cert_.set_trx_committed(ts));
                apply_monitor_.leave(ao);
                report_last_committed(safe_to_discard);
            }
        }
        else
        {
            assert(0); //remove if()
        }
    }
    else
    {
        log_info << "release_rollback() trx: " << trx << ", ts: nil";
    }

    // Trx was either rolled back by user or via certification failure,
    // last committed report not needed since cert index state didn't change.
    // report_last_committed();
    ++local_rollbacks_;

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::sync_wait(wsrep_gtid_t* upto,
                                                int           tout,
                                                wsrep_gtid_t* gtid)
{
    gu::GTID wait_gtid;

    if (upto == 0)
    {
        long ret = gcs_.caused(wait_gtid);
        if (ret < 0)
        {
            log_warn << "gcs_caused() returned " << ret
                     << " ("  << strerror(-ret) << ')';
            return WSREP_TRX_FAIL;
        }
    }
    else
    {
        wait_gtid.set(upto->uuid, upto->seqno);
    }

    try
    {
        // @note: Using timed wait for monitor is currently a hack
        // to avoid deadlock resulting from race between monitor wait
        // and drain during configuration change. Instead of this,
        // monitor should have proper mechanism to interrupt waiters
        // at monitor drain and disallowing further waits until
        // configuration change related operations (SST etc) have been
        // finished.
        gu::datetime::Period timeout(causal_read_timeout_);
        if (tout != -1)
        {
            timeout = gu::datetime::Period(tout * gu::datetime::Sec);
        }
        gu::datetime::Date wait_until(gu::datetime::Date::calendar() + timeout);

        // Note: Since wsrep API 26 application may request release of
        // commit monitor before the commit actually happens (commit
        // may have been ordered/queued on application side for later
        // processing). Therefore we now rely on apply_monitor on sync
        // wait. This is sufficient since apply_monitor is always released
        // only after the whole transaction is over.
        apply_monitor_.wait(wait_gtid, wait_until);

        if (gtid != 0)
        {
            commit_monitor_.last_left_gtid(*gtid);
        }
        ++causal_reads_;
        return WSREP_OK;
    }
    catch (gu::NotFound& e)
    {
        log_debug << "monitor wait failed for sync_wait: UUID mismatch";
        return WSREP_TRX_MISSING;
    }
    catch (gu::Exception& e)
    {
        log_debug << "monitor wait failed for sync_wait: " << e.what();
        return WSREP_TRX_FAIL;
    }
}


wsrep_status_t galera::ReplicatorSMM::last_committed_id(wsrep_gtid_t* gtid)
{
    commit_monitor_.last_left_gtid(*gtid);
    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::to_isolation_begin(TrxHandleMaster&  trx,
                                                         wsrep_trx_meta_t* meta)
{
    assert(trx.locked());

    TrxHandleSlavePtr ts_ptr(trx.ts());
    TrxHandleSlave& ts(*ts_ptr);

    if (meta != 0)
    {
        assert(meta->gtid.seqno > 0);
        assert(meta->gtid.seqno == ts.global_seqno());
        assert(meta->depends_on == ts.depends_seqno());
    }

    assert(trx.state() == TrxHandle::S_REPLICATING);
    assert(trx.trx_id() == static_cast<wsrep_trx_id_t>(-1));
    assert(ts.local_seqno() > -1 && ts.global_seqno() > -1);
    assert(ts.global_seqno() > STATE_SEQNO());

    CommitOrder co(ts, co_mode_);
    wsrep_status_t const retval(cert_and_catch(&trx, ts_ptr));
    switch (retval)
    {
    case WSREP_OK:
    {

        trx.set_state(TrxHandle::S_APPLYING);
        ts.set_state(TrxHandle::S_APPLYING);

        ApplyOrder ao(ts);
        gu_trace(apply_monitor_.enter(ao));

        trx.set_state(TrxHandle::S_COMMITTING);
        ts.set_state(TrxHandle::S_COMMITTING);
        break;
    }
    case WSREP_TRX_FAIL:
        // Apply monitor is released in cert() in case of failure.
        assert(trx.state() == TrxHandle::S_ABORTING);
        break;
    default:
        assert(0);
        gu_throw_fatal << "unrecognized retval " << retval
                       << " for to isolation certification for " << ts;
        break;
    }

    if (co_mode_ != CommitOrder::BYPASS)
        try
        {
            commit_monitor_.enter(co);

            if (ts.state() == TrxHandle::S_COMMITTING)
            {
                log_debug << "Executing TO isolated action: " << ts;
                st_.mark_unsafe();
            }
            else
            {
                log_debug << "Grabbed TO for failed isolated action: " << ts;
                assert(trx.state() == TrxHandle::S_ABORTING);
            }
        }
        catch (...)
        {
            gu_throw_fatal << "unable to enter commit monitor: " << ts;
        }

    return retval;
}


wsrep_status_t
galera::ReplicatorSMM::to_isolation_end(TrxHandleMaster&         trx,
                                        const wsrep_buf_t* const err)
{
    TrxHandleSlavePtr ts_ptr(trx.ts());
    TrxHandleSlave& ts(*ts_ptr);

    log_debug << "Done executing TO isolated action: " << ts
              << ", error message: " << gu::Hexdump(err->ptr, err->len, true);
    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_ABORTING);
    assert(ts.state() == TrxHandle::S_COMMITTING ||
           ts.state() == TrxHandle::S_ABORTING);

    CommitOrder co(ts, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.leave(co);
    report_last_committed(cert_.set_trx_committed(ts));

    if (ts.state() == TrxHandle::S_COMMITTING)
    {
        ApplyOrder ao(ts);
        apply_monitor_.leave(ao);
        assert(trx.state() == TrxHandle::S_COMMITTING);
        trx.set_state(TrxHandle::S_COMMITTED);
        ts.set_state(TrxHandle::S_COMMITTED);

        st_.mark_safe();
    }
    else
    {
        // apply_monitor_ was canceled in cert()
        assert(trx.state() == TrxHandle::S_ABORTING);
        assert(ts.state() == TrxHandle::S_ABORTING);
        trx.set_state(TrxHandle::S_ROLLED_BACK);
        ts.set_state(TrxHandle::S_ROLLED_BACK);
    }

    return WSREP_OK;
}


namespace galera
{

static WriteSetOut*
writeset_from_handle (wsrep_po_handle_t&             handle,
                      const TrxHandleMaster::Params& trx_params)
{
    WriteSetOut* ret = reinterpret_cast<WriteSetOut*>(handle.opaque);

    if (NULL == ret)
    {
        try
        {
            ret = new WriteSetOut(
//                gu::String<256>(trx_params.working_dir_) << '/' << &handle,
                trx_params.working_dir_, wsrep_trx_id_t(&handle),
                /* key format is not essential since we're not adding keys */
                KeySet::version(trx_params.key_format_), NULL, 0, 0,
                WriteSetNG::MAX_VERSION, DataSet::MAX_VERSION, DataSet::MAX_VERSION,
                trx_params.max_write_set_size_);

            handle.opaque = ret;
        }
        catch (std::bad_alloc& ba)
        {
            gu_throw_error(ENOMEM) << "Could not create WriteSetOut";
        }
    }

    return ret;
}

} /* namespace galera */

wsrep_status_t
galera::ReplicatorSMM::preordered_collect(wsrep_po_handle_t&            handle,
                                          const struct wsrep_buf* const data,
                                          size_t                  const count,
                                          bool                    const copy)
{
    WriteSetOut* const ws(writeset_from_handle(handle, trx_params_));

    for (size_t i(0); i < count; ++i)
    {
        ws->append_data(data[i].ptr, data[i].len, copy);
    }

    return WSREP_OK;
}


wsrep_status_t
galera::ReplicatorSMM::preordered_commit(wsrep_po_handle_t&         handle,
                                         const wsrep_uuid_t&        source,
                                         uint64_t             const flags,
                                         int                  const pa_range,
                                         bool                 const commit)
{
    WriteSetOut* const ws(writeset_from_handle(handle, trx_params_));

    if (gu_likely(true == commit))
    {
        assert(source != WSREP_UUID_UNDEFINED);

        ws->set_flags (WriteSetNG::wsrep_flags_to_ws_flags(flags) |
                       WriteSetNG::F_PREORDERED);

        /* by loooking at trx_id we should be able to detect gaps / lost events
         * (however resending is not implemented yet). Something like
         *
         * wsrep_trx_id_t const trx_id(cert_.append_preordered(source, ws));
         *
         * begs to be here. */
        wsrep_trx_id_t const trx_id(preordered_id_.add_and_fetch(1));

        WriteSetNG::GatherVector actv;

        size_t const actv_size(ws->gather(source, 0, trx_id, actv));

        ws->finalize_preordered(pa_range); // also adds checksum

        int rcode;
        do
        {
            rcode = gcs_.sendv(actv, actv_size, GCS_ACT_WRITESET, false, false);
        }
        while (rcode == -EAGAIN && (usleep(1000), true));

        if (rcode < 0)
            gu_throw_error(-rcode)
                << "Replication of preordered writeset failed.";
    }

    delete ws; // cleanup regardless of commit flag

    handle.opaque = NULL;

    return WSREP_OK;
}


wsrep_status_t
galera::ReplicatorSMM::sst_sent(const wsrep_gtid_t& state_id, int rcode)
{
    assert (rcode <= 0);
    assert (rcode == 0 || state_id.seqno == WSREP_SEQNO_UNDEFINED);
    assert (rcode != 0 || state_id.seqno >= 0);

    if (state_() != S_DONOR)
    {
        log_error << "sst sent called when not SST donor, state " << state_();
        return WSREP_CONN_FAIL;
    }

    if (state_id.uuid != state_uuid_ && rcode >= 0)
    {
        // state we have sent no longer corresponds to the current group state
        // mark an error
        rcode = -EREMCHG;
    }

    try {
        if (rcode == 0)
            gcs_.join(gu::GTID(state_id.uuid, state_id.seqno), rcode);
        else
            /* stamp error message with the current state */
            gcs_.join(gu::GTID(state_uuid_, commit_monitor_.last_left()), rcode);

        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << "failed to recover from DONOR state: " << e.what();
        return WSREP_CONN_FAIL;
    }
}

void galera::ReplicatorSMM::process_trx(void* recv_ctx,
                                        const TrxHandleSlavePtr& ts_ptr)
{
    assert(recv_ctx != 0);
    assert(ts_ptr != 0);

    TrxHandleSlave& ts(*ts_ptr);

    assert(ts.local_seqno() > 0);
    assert(ts.global_seqno() > 0);
    assert(ts.last_seen_seqno() >= 0);
    assert(ts.depends_seqno() == -1 || ts.version() >= 4);
    assert(ts.state() == TrxHandle::S_REPLICATING);

    wsrep_status_t const retval(cert_and_catch(0, ts_ptr));
    switch (retval)
    {
    case WSREP_TRX_FAIL:
        assert(ts.state() == TrxHandle::S_ABORTING);
        /* fall through to apply_trx() */
    case WSREP_OK:
        try
        {
            gu_trace(apply_trx(recv_ctx, ts));
        }
        catch (std::exception& e)
        {
            log_fatal << "Failed to apply trx: " << ts;
            log_fatal << e.what();
            log_fatal << "Node consistency compromized, leaving cluster...";
            mark_corrupt_and_close();
            assert(0); // this is an unexpected exception
            // keep processing events from the queue until provider is closed
        }
        break;
    case WSREP_TRX_MISSING: // must be skipped due to SST
        assert(ts.state() == TrxHandle::S_ABORTING);
        report_last_committed(cert_.set_trx_committed(ts));
        break;
    default:
        // this should not happen for remote actions
        gu_throw_error(EINVAL)
            << "unrecognized retval for remote trx certification: "
            << retval << " trx: " << ts;
    }
}


void galera::ReplicatorSMM::process_commit_cut(wsrep_seqno_t const seq,
                                               wsrep_seqno_t const seqno_l)
{
    assert(seq > 0);
    assert(seqno_l > 0);

    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    if (seq >= cc_seqno_) /* Refs #782. workaround for
                           * assert(seqno >= seqno_released_) in gcache. */
        cert_.purge_trxs_upto(seq, true);

    local_monitor_.leave(lo);
    log_debug << "Got commit cut from GCS: " << seq;
}


/* NB: the only use for this method is in cancel_seqnos() below */
void galera::ReplicatorSMM::cancel_seqno(wsrep_seqno_t const seqno)
{
    assert(seqno > 0);

    ApplyOrder ao(seqno, seqno - 1);
    apply_monitor_.self_cancel(ao);

    if (co_mode_ != CommitOrder::BYPASS)
    {
        CommitOrder co(seqno, co_mode_);
        commit_monitor_.self_cancel(co);
    }
}

/* NB: the only use for this method is to dismiss the slave queue
 *     in corrupt state */
void galera::ReplicatorSMM::cancel_seqnos(wsrep_seqno_t const seqno_l,
                                          wsrep_seqno_t const seqno_g)
{
    if (seqno_l > 0)
    {
        LocalOrder lo(seqno_l);
        local_monitor_.self_cancel(lo);
    }

    if (seqno_g > 0) cancel_seqno(seqno_g);
}


void galera::ReplicatorSMM::drain_monitors(wsrep_seqno_t const upto)
{
    apply_monitor_.drain(upto);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);
}


void galera::ReplicatorSMM::set_initial_position(const wsrep_uuid_t&  uuid,
                                                 wsrep_seqno_t const seqno)
{
    update_state_uuid(uuid);

    apply_monitor_.set_initial_position(uuid, seqno);
    if (co_mode_ != CommitOrder::BYPASS)
        commit_monitor_.set_initial_position(uuid, seqno);
}

void galera::ReplicatorSMM::establish_protocol_versions (int proto_ver)
{
    switch (proto_ver)
    {
    case 1:
        trx_params_.version_ = 1;
        str_proto_ver_ = 0;
        break;
    case 2:
        trx_params_.version_ = 1;
        str_proto_ver_ = 1;
        break;
    case 3:
    case 4:
        trx_params_.version_ = 2;
        str_proto_ver_ = 1;
        break;
    case 5:
        trx_params_.version_ = 3;
        str_proto_ver_ = 1;
        break;
    case 6:
        trx_params_.version_  = 3;
        str_proto_ver_ = 2; // gcs intelligent donor selection.
        // include handling dangling comma in donor string.
        break;
    case 7:
        // Protocol upgrade to handle IST SSL backwards compatibility,
        // no effect to TRX or STR protocols.
        trx_params_.version_ = 3;
        str_proto_ver_ = 2;
        break;
    case 8:
        trx_params_.version_ = 4;
        str_proto_ver_ = 3;
        break;
    default:
        log_fatal << "Configuration change resulted in an unsupported protocol "
            "version: " << proto_ver << ". Can't continue.";
        abort();
    };

    protocol_version_ = proto_ver;
    log_info << "REPL Protocols: " << protocol_version_ << " ("
             << trx_params_.version_ << ", " << str_proto_ver_ << ")";
}

void
galera::ReplicatorSMM::update_incoming_list(const wsrep_view_info_t& view)
{
    static char const separator(',');

    ssize_t new_size(0);

    if (view.memb_num > 0)
    {
        new_size += view.memb_num - 1; // separators

        for (int i = 0; i < view.memb_num; ++i)
        {
            new_size += strlen(view.members[i].incoming);
        }
    }

    gu::Lock lock(incoming_mutex_);

    incoming_list_.clear();
    incoming_list_.resize(new_size);

    if (new_size <= 0) return;

    incoming_list_ = view.members[0].incoming;

    for (int i = 1; i < view.memb_num; ++i)
    {
        incoming_list_ += separator;
        incoming_list_ += view.members[i].incoming;
    }
}

static galera::Replicator::State state2repl(gcs_node_state const my_state,
                                            int const            my_idx)
{
    switch (my_state)
    {
    case GCS_NODE_STATE_NON_PRIM:
    case GCS_NODE_STATE_PRIM:
        return galera::Replicator::S_CONNECTED;
    case GCS_NODE_STATE_JOINER:
        return galera::Replicator::S_JOINING;
    case GCS_NODE_STATE_JOINED:
        return galera::Replicator::S_JOINED;
    case GCS_NODE_STATE_SYNCED:
        return galera::Replicator::S_SYNCED;
    case GCS_NODE_STATE_DONOR:
        return galera::Replicator::S_DONOR;
    case GCS_NODE_STATE_MAX:
        assert(0);
    }

    gu_throw_fatal << "unhandled gcs state: " << my_state;
    GU_DEBUG_NORETURN;
}

void
galera::ReplicatorSMM::process_conf_change(void*                    recv_ctx,
                                           const struct gcs_action& cc)
{
    assert(cc.seqno_l > -1);

    gcs_act_cchange const conf(cc.buf, cc.size);

    bool const from_IST(0 == cc.seqno_l);

    LocalOrder lo(cc.seqno_l);

    if (!from_IST)
    {
        gu_trace(local_monitor_.enter(lo));
    }

    assert(!from_IST || WSREP_UUID_UNDEFINED != uuid_);

    int const prev_protocol_version(protocol_version_);

    if (conf.conf_id >= 0) // Primary configuration
    {
        establish_protocol_versions (conf.repl_proto_ver);
        assert(!from_IST || conf.repl_proto_ver >= 8);
    }

    // we must have either my_idx or uuid_ defined
    assert(cc.seqno_g >= 0 || uuid_ != WSREP_UUID_UNDEFINED);

    wsrep_uuid_t new_uuid(uuid_);
    wsrep_view_info_t* const view_info
        (galera_view_info_create(conf, (!from_IST ? cc.seqno_g : -1), new_uuid));
    if (view_info->status == WSREP_VIEW_PRIMARY)
    {
        safe_to_bootstrap_ = (view_info->memb_num == 1);
    }

    int const my_idx(view_info->my_idx);
    gcs_node_state_t const my_state
        (my_idx >= 0 ? conf.memb[my_idx].state_ : GCS_NODE_STATE_NON_PRIM);

    assert(my_state >= GCS_NODE_STATE_NON_PRIM);
    assert(my_state < GCS_NODE_STATE_MAX);

    wsrep_seqno_t const group_seqno(view_info->state_id.seqno);
    const wsrep_uuid_t& group_uuid (view_info->state_id.uuid);
    assert(group_seqno == conf.seqno);

    if (!from_IST)
    {
        bool first_view(false);
        if (WSREP_UUID_UNDEFINED == uuid_)
        {
            uuid_ = new_uuid;
            first_view = true;
        }
        else
        {
            if (view_info-> memb_num > 0 && view_info->my_idx < 0)
                // something went wrong, member must be present in own view
            {
                std::stringstream msg;

                msg << "Node UUID " << uuid_ << " is absent from the view:\n";

                for (int m(0); m < view_info->memb_num; ++m)
                {
                    msg << '\t' << view_info->members[m].id << '\n';
                }

                msg << "most likely due to unexpected node identity change. "
                    "Aborting.";

                log_fatal << msg.str();

                abort();
            }
        }

        log_info << "####### My UUID: " << uuid_;

        if (conf.seqno != WSREP_SEQNO_UNDEFINED &&
            conf.seqno <= sst_seqno_)
        {
            log_info << "####### skipping CC " << conf.seqno
                     << (from_IST ? ", from IST" : ", local");

            // applied already in SST/IST, skip
            gu_trace(local_monitor_.leave(lo));
            resume_recv();
            gcache_.free(const_cast<void*>(cc.buf));
            return;
        }
        else
        {
            wsrep_seqno_t const upto(cert_.position());
            gu_trace(drain_monitors(upto));
            // IST recv thread drains monitors itself
        }

        // First view from the group or group uuid has changed,
        // call connected callback to notify application.
        if ((first_view || state_uuid_ != group_uuid) && connected_cb_)
        {
            wsrep_cb_status_t cret(connected_cb_(0, view_info));
            if (cret != WSREP_CB_SUCCESS)
            {
                log_fatal << "Application returned error "
                          << cret
                          << " from connect callback, aborting";
                abort();
            }
        }
    }

    update_incoming_list(*view_info);

    log_info << "####### processing CC " << conf.seqno
             << (from_IST ? ", from IST" : ", local");

    bool const st_required
        (state_transfer_required(*view_info, my_state == GCS_NODE_STATE_PRIM));

    void*  app_req(0);
    size_t app_req_len(0);
#ifndef NDEBUG
    bool   app_waits_sst(false);
#endif

    if (st_required)
    {
        assert(!from_IST);

        log_info << "State transfer required: "
                 << "\n\tGroup state: " << group_uuid << ":" << group_seqno
                 << "\n\tLocal state: " << state_uuid_<< ":" << STATE_SEQNO();

        if (S_CONNECTED != state_()) state_.shift_to(S_CONNECTED);

        wsrep_cb_status_t const rcode(sst_request_cb_(&app_req, &app_req_len));

        if (WSREP_CB_SUCCESS != rcode)
        {
            assert(app_req_len <= 0);
            log_fatal << "SST request callback failed. This is unrecoverable, "
                      << "restart required.";
            abort();
        }
        else if (0 == app_req_len && state_uuid_ != group_uuid)
        {
            log_fatal << "Local state UUID " << state_uuid_
                      << " is different from group state UUID " << group_uuid
                      << ", and SST request is null: restart required.";
            abort();
        }
#ifndef NDEBUG
        app_waits_sst = (app_req_len > 0) &&
            (app_req_len != (strlen(WSREP_STATE_TRANSFER_NONE) + 1) ||
             memcmp(app_req, WSREP_STATE_TRANSFER_NONE, app_req_len));
#endif
    }
    else
    {
        log_info << "####### ST not required";
    }

    Replicator::State const next_state(state2repl(my_state, my_idx));

    if (conf.conf_id >= 0) // Primary configuration
    {
        // if protocol version >= 8, first CC already carries seqno 1,
        // so it can't be less than 1. For older protocols it can be 0.
        assert(group_seqno >= (protocol_version_ >= 8));

        //
        // Starting from protocol_version_ 8 joiner's cert index is rebuilt
        // from IST.
        //
        // The reasons to reset cert index:
        // - Protocol version lower than 8    (ALL)
        // - Protocol upgrade                 (ALL)
        // - State transfer will take a place (JOINER)
        //
        bool index_reset(protocol_version_ < 8 ||
                         prev_protocol_version != protocol_version_ ||
                         // this last condition is a bit too strict. In fact
                         // checking for app_waits_sst would be enough, but in
                         // that case we'd have to skip cert index rebuilding
                         // when there is none.
                         // This would complicate the logic with little to no
                         // benefits...
                         st_required);

        if (index_reset)
        {
            gu::GTID position;

            if (protocol_version_ < 8)
            {
                position.set(group_uuid, group_seqno);
            }
            else
            {
                position.set(GU_UUID_NIL, 0);
            }

            /* 2 reasons for this here:
             * 1 - compatibility with protocols < 8
             * 2 - preparing cert index for preloading by setting seqno to 0 */
            log_info << "Cert index reset to " << position << " (proto: "
                     << protocol_version_ << "), state transfer needed: "
                     << (st_required ? "yes" : "no");
            /* flushes service thd, must be called before gcache_.seqno_reset()*/
            cert_.assign_initial_position(position, trx_params_.version_);
        }
        else
        {
            log_info << "Skipping cert index reset";
        }

        // This event can be processed 2 times:
        // 1) out-of-order when state transfer is required
        // 2) in-order (either when no state transfer or IST)
        // When doing it out of order, the event buffer is simply discarded
        if (st_required)
        {
            assert(!from_IST); // make sure we are never here from IST

            gu_trace(gcache_.free(const_cast<void*>(cc.buf)));

            // GCache::seqno_reset() happens here
            request_state_transfer (recv_ctx,
                                    group_uuid, group_seqno, app_req,
                                    app_req_len);
        }
        else if (conf.seqno > cert_.position())
        {
            assert(!app_waits_sst);

            /* since CC does not pass certification, need to adjust cert
             * position explicitly (when processed in order) */
            /* flushes service thd, must be called before gcache_.seqno_reset()*/
            cert_.adjust_position(*view_info, gu::GTID(group_uuid, group_seqno),
                                  trx_params_.version_);

            log_info << "####### Setting monitor position to " << group_seqno;
            set_initial_position(group_uuid, group_seqno - 1);
            cancel_seqno(group_seqno); // cancel CC seqno

            if (!from_IST)
            {
                /* CCs from IST already have seqno assigned and cert. position
                 * adjusted */

                if (protocol_version_ >= 8)
                {
                    gu_trace(gcache_.seqno_assign(cc.buf, conf.seqno,
                                                  GCS_ACT_CCHANGE, false));
                }
                else /* before protocol ver 8 conf changes are not ordered */
                {
                    gu_trace(gcache_.free(const_cast<void*>(cc.buf)));
                }

                if (state_() == S_CONNECTED || state_() == S_DONOR)
                {
                    switch (next_state)
                    {
                    case S_JOINING:
                        state_.shift_to(S_JOINING);
                        break;
                    case S_DONOR:
                        if (state_() == S_CONNECTED)
                        {
                            state_.shift_to(S_DONOR);
                        }
                        break;
                    case S_JOINED:
                        state_.shift_to(S_JOINED);
                        break;
                    case S_SYNCED:
                        state_.shift_to(S_SYNCED);
                        if (synced_cb_(app_ctx_) != WSREP_CB_SUCCESS)
                        {
                            log_fatal << "Synced callback failed. This is "
                                      << "unrecoverable, restart required.";
                            abort();
                        }
                        break;
                    default:
                        log_debug << "next_state " << next_state;
                        break;
                    }
                }
            }

            // record state seqno, needed for IST on DONOR
            cc_seqno_ = group_seqno;
            // Record lowest trx seqno in cert index to set cert index
            // rebuild flag appropriately in IST. Notice that if cert index
            // was completely reset above, the value returned is zero and
            // no rebuild should happen.
            cc_lowest_trx_seqno_ = cert_.lowest_trx_seqno();
            log_info << "####### Lowest cert index boundary: "
                     << cc_lowest_trx_seqno_;
            log_info << "####### Min available from gcache: "
                     << gcache_.seqno_min();
            assert(gcache_.seqno_min() > 0);
            assert(cc_lowest_trx_seqno_ >= gcache_.seqno_min());

            st_.set(state_uuid_, WSREP_SEQNO_UNDEFINED, safe_to_bootstrap_);
        }
        else
        {
            assert(!from_IST);
        }

        if (!from_IST && state_() == S_JOINING && sst_state_ != SST_NONE)
        {
            /* There are two reasons we can be here:
             * 1) we just got state transfer in request_state_transfer() above;
             * 2) we failed here previously (probably due to partition).
             */
            try {
                gcs_.join(gu::GTID(state_uuid_, sst_seqno_), 0);
                sst_state_ = SST_NONE;
            }
            catch (gu::Exception& e)
            {
                log_error << "Failed to JOIN the cluster after SST";
            }
        }
    }
    else
    {
        // Non-primary configuration
        assert(conf.seqno == WSREP_SEQNO_UNDEFINED);

        // reset sst_seqno_ every time we disconnct from PC
        sst_seqno_ = WSREP_SEQNO_UNDEFINED;

        if (state_uuid_ != WSREP_UUID_UNDEFINED)
        {
            st_.set (state_uuid_, STATE_SEQNO(), safe_to_bootstrap_);
        }

        gcache_.free(const_cast<void*>(cc.buf));

        gu::Lock lock(closing_mutex_);

        if (S_CONNECTED != next_state)
        {
            log_fatal << "Internal error: unexpected next state for "
                      << "non-prim: " << next_state
                      << ". Current state: " << state_() <<". Restart required.";
            abort();
        }

        if (state_() > S_CONNECTED)
        {
            assert(S_CONNECTED == next_state);
            state_.shift_to(S_CONNECTED);
        }
    }

    free(app_req);

    if (!st_required              /* in-order processing  */ ||
        sst_seqno_ >= group_seqno /* SST "ate" this event */)
    {
        wsrep_cb_status_t const rcode
            (view_cb_(app_ctx_, recv_ctx, view_info, 0, 0));

        if (WSREP_CB_SUCCESS != rcode) // is this really fatal now?
        {
            log_fatal << "View callback failed. This is unrecoverable, "
                      << "restart required.";
            abort();
        }
    }
    free(view_info);

    if (!from_IST)
    {
        double foo, bar;
        size_t index_size;
        cert_.stats_get(foo, bar, index_size);
        local_monitor_.leave(lo);
        resume_recv();
    }

    if (conf.conf_id < 0 && conf.memb.size() == 0) {
        log_debug << "Received SELF-LEAVE. Connection closed.";
        assert(cc.seqno_l > 0);

        gu::Lock lock(closing_mutex_);

        shift_to_CLOSED();
    }
}


void galera::ReplicatorSMM::process_join(wsrep_seqno_t seqno_j,
                                         wsrep_seqno_t seqno_l)
{
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());
    drain_monitors(upto);

    if (seqno_j < 0 && S_JOINING == state_())
    {
        // #595, @todo: find a way to re-request state transfer
        log_fatal << "Failed to receive state transfer: " << seqno_j
                  << " (" << strerror (-seqno_j) << "), need to restart.";
        abort();
    }
    else
    {
        state_.shift_to(S_JOINED);
    }

    local_monitor_.leave(lo);
}


void galera::ReplicatorSMM::process_sync(wsrep_seqno_t seqno_l)
{
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());
    drain_monitors(upto);

    state_.shift_to(S_SYNCED);
    if (synced_cb_(app_ctx_) != WSREP_CB_SUCCESS)
    {
        log_fatal << "Synced callback failed. This is unrecoverable, "
                  << "restart required.";
        abort();
    }
    local_monitor_.leave(lo);
}

wsrep_seqno_t galera::ReplicatorSMM::pause()
{
    // Grab local seqno for local_monitor_
    wsrep_seqno_t const local_seqno(
        static_cast<wsrep_seqno_t>(gcs_.local_sequence()));
    LocalOrder lo(local_seqno);
    local_monitor_.enter(lo);

    // Local monitor should take care that concurrent
    // pause requests are enqueued
    assert(pause_seqno_ == WSREP_SEQNO_UNDEFINED);
    pause_seqno_ = local_seqno;

    // Get drain seqno from cert index
    wsrep_seqno_t const upto(cert_.position());
    drain_monitors(upto);

    assert (apply_monitor_.last_left() >= upto);
    if (co_mode_ != CommitOrder::BYPASS)
    {
        assert (commit_monitor_.last_left() >= upto);
        assert (commit_monitor_.last_left() == apply_monitor_.last_left());
    }

    wsrep_seqno_t const ret(STATE_SEQNO());
    st_.set(state_uuid_, ret, safe_to_bootstrap_);

    log_info << "Provider paused at " << state_uuid_ << ':' << ret
             << " (" << pause_seqno_ << ")";

    return ret;
}

void galera::ReplicatorSMM::resume()
{
    if (pause_seqno_ == WSREP_SEQNO_UNDEFINED)
    {
        log_warn << "tried to resume unpaused provider";
        return;
    }

    st_.set(state_uuid_, WSREP_SEQNO_UNDEFINED, safe_to_bootstrap_);
    log_info << "resuming provider at " << pause_seqno_;
    LocalOrder lo(pause_seqno_);
    pause_seqno_ = WSREP_SEQNO_UNDEFINED;
    local_monitor_.leave(lo);
    log_info << "Provider resumed.";
}

void galera::ReplicatorSMM::desync()
{
    wsrep_seqno_t seqno_l;

    ssize_t const ret(gcs_.desync(seqno_l));

    if (seqno_l > 0)
    {
        LocalOrder lo(seqno_l); // need to process it regardless of ret value

        if (ret == 0)
        {
/* #706 - the check below must be state request-specific. We are not holding
          any locks here and must be able to wait like any other action.
          However practice may prove different, leaving it here as a reminder.
            if (local_monitor_.would_block(seqno_l))
            {
                gu_throw_error (-EDEADLK) << "Ran out of resources waiting to "
                                          << "desync the node. "
                                          << "The node must be restarted.";
            }
*/
            local_monitor_.enter(lo);
            if (state_() != S_DONOR) state_.shift_to(S_DONOR);
            local_monitor_.leave(lo);
        }
        else
        {
            local_monitor_.self_cancel(lo);
        }
    }

    if (ret)
    {
        gu_throw_error (-ret) << "Node desync failed.";
    }
}

void galera::ReplicatorSMM::resync()
{
    gcs_.join(gu::GTID(state_uuid_, commit_monitor_.last_left()), 0);
}


//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
////                           Private
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

/* don't use this directly, use cert_and_catch() instead */
inline
wsrep_status_t galera::ReplicatorSMM::cert(TrxHandleMaster* trx,
                                           const TrxHandleSlavePtr& ts)
{
    assert(trx == 0 ||
           (trx->state() == TrxHandle::S_REPLICATING ||
            trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY));
    assert(ts->state() == TrxHandle::S_REPLICATING ||
           ts->state() == TrxHandle::S_CERTIFYING);

    assert(ts->local_seqno()     != WSREP_SEQNO_UNDEFINED);
    assert(ts->global_seqno()    != WSREP_SEQNO_UNDEFINED);
    assert(ts->last_seen_seqno() >= 0);
    assert(ts->last_seen_seqno() < ts->global_seqno());

    LocalOrder lo(*ts);
    bool       interrupted(false);
    bool       in_replay(trx != 0 &&
                         trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY);

    try
    {
        if (trx != 0)
        {
            if (in_replay == false) trx->set_state(TrxHandle::S_CERTIFYING);
            trx->unlock();
        }
        if (in_replay == false || local_monitor_.entered(lo) == false)
        {
            gu_trace(local_monitor_.enter(lo));
        }
        if (trx != 0) trx->lock();
        assert(trx == 0 ||
               (trx->state() == TrxHandle::S_CERTIFYING ||
                trx->state() == TrxHandle::S_MUST_ABORT ||
                trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY));
    }
    catch (gu::Exception& e)
    {
        if (trx != 0) trx->lock();
        if (e.get_errno() == EINTR) { interrupted = true; }
        else throw;
    }

    wsrep_status_t retval(WSREP_OK);
    bool const applicable(ts->global_seqno() > STATE_SEQNO());
    assert(!ts->local() || applicable); // applicable can't be false for locals

    if (gu_unlikely (interrupted ||
                     (trx != 0 && trx->state() == TrxHandle::S_MUST_ABORT)))
    {
//remove        assert(trx == 0 || trx->state() == TrxHandle::S_MUST_ABORT);
        assert(trx != 0);
        retval = cert_for_aborted(ts);

#if 0 //remove
        if (WSREP_TRX_FAIL == retval)
        {
            assert(WSREP_SEQNO_UNDEFINED == ts->depends_seqno());
            assert(TrxHandle::S_ABORTING == ts->state());

            if (interrupted == true)
            {
                local_monitor_.self_cancel(lo);
            }
            else
            {
                local_monitor_.leave(lo);
            }

            if (trx != 0) trx->set_state(TrxHandle::S_ABORTING);
        }
        else
#endif //0
        if (WSREP_TRX_FAIL != retval)
        {
            assert(ts->state() == TrxHandle::S_REPLICATING ||
                   ts->state() == TrxHandle::S_CERTIFYING);
            assert(WSREP_BF_ABORT == retval);
            assert(trx != 0);

//remove            if (trx != 0)
//            {
                // If the transaction was committing, it must replay.
                if (ts->flags() & TrxHandle::F_COMMIT)
                {
                    trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
                    return retval;
                }
                else
                {
//remove                    trx->reset_ts();
                    pending_cert_queue_.push(ts);

                    ts->set_state(TrxHandle::S_ABORTING);

#if 0 //remove
                    trx->set_state(TrxHandle::S_ABORTING);
                    if (interrupted == true)
                    {
                        local_monitor_.self_cancel(lo);
                    }
                    else
                    {
                        local_monitor_.leave(lo);
                    }

                    // If not ts->must_neter_am(), apply_monitor_
                    // will be canceled at the end of the method,
                    // in cancel_monitors().
//remove                    if (ts->pa_unsafe())
                    if (ts->must_enter_am())
                    {
                        ApplyOrder ao(*ts);
                        apply_monitor_.self_cancel(ao);
                    }
//#if 0 //remove
                    if (co_mode_ != CommitOrder::BYPASS)
                    {
                        CommitOrder co(*ts, co_mode_);
                        commit_monitor_.self_cancel(co);
                    }
#endif //0
                    retval = WSREP_TRX_FAIL;
                }
        }
        else
        {
            assert(WSREP_TRX_FAIL == retval);
            assert(WSREP_SEQNO_UNDEFINED == ts->depends_seqno());
        }

        if (WSREP_TRX_FAIL == retval)
        {
                    assert(TrxHandle::S_ABORTING == ts->state());

                    if (trx) trx->set_state(TrxHandle::S_ABORTING);

                    if (interrupted == true)
                    {
                        local_monitor_.self_cancel(lo);
                    }
                    else
                    {
                        local_monitor_.leave(lo);
                    }

                    // If not ts->must_neter_am(), apply_monitor_
                    // will be canceled at the end of the method,
                    // in cancel_monitors().
//remove                    if (ts->pa_unsafe())
#if 0 // remove
                    if (ts->must_enter_am())
                    {
                        ApplyOrder ao(*ts);
                        apply_monitor_.self_cancel(ao);
                    }
#endif
        }
        else return retval;
//remove        }
    }
    else
    {
        ts->set_state(TrxHandle::S_CERTIFYING);

        // pending_cert_queue_ contains all writesets that:
        //   a) were BF aborted before being certified
        //   b) are not going to be replayed even though
        //      cert_for_aborted() returned TEST_OK for them
        //
        // Before certifying the current seqno, check if
        // pending_cert_queue contains any smaller seqno.
        // This avoids the certification index to diverge
        // across nodes.
        TrxHandleSlavePtr aborted_ts;
        while ((aborted_ts = pending_cert_queue_.must_cert_next(ts->global_seqno()))
               != NULL)
        {
            log_debug << "must cert next " << ts->global_seqno()
                      << " aborted ts " << *aborted_ts;

            Certification::TestResult result;
            result = cert_.append_trx(aborted_ts);
            report_last_committed(cert_.set_trx_committed(*aborted_ts));
//remove            aborted_ts->set_state(TrxHandle::S_ROLLED_BACK);

            log_debug << "trx in pending cert queue certified, result: "
                      << result;
        }

        switch (cert_.append_trx(ts))
        {
        case Certification::TEST_OK:
            if (gu_likely(applicable))
            {
                retval = WSREP_OK;
                assert(ts->depends_seqno() >= 0);
            }
            else
            {
                // this can happen after SST position has been submitted
                // but not all actions preceding SST initial position
                // have been processed
                if (trx != 0) trx->set_state(TrxHandle::S_ABORTING);
                ts->set_state(TrxHandle::S_ABORTING);
                retval = WSREP_TRX_MISSING;
            }
            break;
        case Certification::TEST_FAILED:
            if (gu_unlikely(ts->is_toi() && applicable)) // small sanity check
            {
                // may happen on configuration change
                log_warn << "Certification failed for TO isolated action: "
                         << *trx;
                assert(0);
            }
            local_cert_failures_ += ts->local();
            if (trx != 0) trx->set_state(TrxHandle::S_ABORTING);
            retval = applicable ? WSREP_TRX_FAIL : WSREP_TRX_MISSING;
            break;
        }

        // at this point we are about to leave local_monitor_. Make sure
        // trx checksum was alright before that.
        ts->verify_checksum();

        // we must do it 'in order' for std::map reasons, so keeping
        // it inside the monitor
        gcache_.seqno_assign (ts->action().first, ts->global_seqno(),
                              GCS_ACT_WRITESET, ts->depends_seqno() < 0);

        if (gu_unlikely(WSREP_TRX_MISSING == retval ||
                        !ts->must_enter_am()))
        {
            // last chance to set trx committed while inside of a monitor
            report_last_committed(cert_.set_trx_committed(*ts));
        }

        local_monitor_.leave(lo);
    }

    assert(WSREP_OK == retval || WSREP_TRX_FAIL == retval ||
           WSREP_TRX_MISSING == retval);

    if (gu_unlikely(WSREP_TRX_FAIL == retval &&
                    (!ts->must_enter_am() || trx != NULL)))
    {
        assert(ts->state() == TrxHandle::S_ABORTING);
        // applicable but failed certification: self-cancel monitors
        cancel_monitors<false>(*ts);
    }
    else
    {
        assert(WSREP_OK != retval || ts->depends_seqno() >= 0);
        if (WSREP_OK != retval && ts->local())
        {
            log_info << "#############" << "Skipped cancel_monitors(): retval: "
                     << retval << ", must_enter_am: " << ts->must_enter_am()
                     << ", trx: " << trx << ", ts: " << *ts;
        }
    }

#if 0
    uint16_t const sid(*reinterpret_cast<const uint16_t*>(&ts->source_id()));
    log_info << "######## certified g: " << ts->global_seqno()
             << ", s: " << ts->last_seen_seqno()
             << ", d: " << ts->depends_seqno()
             << ", sid: " << sid
             << ", retval: " << (retval == WSREP_OK);
#endif

    return retval;
}

/* pretty much any exception in cert() is fatal as it blocks local_monitor_ */
wsrep_status_t galera::ReplicatorSMM::cert_and_catch(
    TrxHandleMaster* trx,
    const TrxHandleSlavePtr& ts)
{
    try
    {
        return cert(trx, ts);
    }
    catch (std::exception& e)
    {
        log_fatal << "Certification exception: " << e.what();
    }
    catch (...)
    {
        log_fatal << "Unknown certification exception";
    }
    assert(0);
    abort();
}

/* This must be called BEFORE local_monitor_.self_cancel() due to
 * gcache_.seqno_assign() */
wsrep_status_t galera::ReplicatorSMM::cert_for_aborted(
    const TrxHandleSlavePtr& ts)
{
    // trx was BF aborted either while it was replicating or
    // while it was waiting for local monitor
    assert(ts->state() == TrxHandle::S_REPLICATING ||
           ts->state() == TrxHandle::S_CERTIFYING);

    Certification::TestResult const res(cert_.test(ts, false));

    switch (res)
    {
    case Certification::TEST_OK:
        return WSREP_BF_ABORT;

    case Certification::TEST_FAILED:
        // Next step will be monitors release. Make sure that ws was not
        // corrupted and cert failure is real before proceeding with that.
 //gcf788 - this must be moved to cert(), the caller method
        assert(ts->is_dummy());
        ts->verify_checksum();
        gcache_.seqno_assign (ts->action().first, ts->global_seqno(),
                              GCS_ACT_WRITESET, true);
        return WSREP_TRX_FAIL;

    default:
        log_fatal << "Unexpected return value from Certification::test(): "
                  << res;
        abort();
    }
}


void
galera::ReplicatorSMM::update_state_uuid (const wsrep_uuid_t& uuid)
{
    if (state_uuid_ != uuid)
    {
        *(const_cast<wsrep_uuid_t*>(&state_uuid_)) = uuid;

        std::ostringstream os; os << state_uuid_;

        strncpy(const_cast<char*>(state_uuid_str_), os.str().c_str(),
                sizeof(state_uuid_str_));
    }

    st_.set(uuid, WSREP_SEQNO_UNDEFINED, safe_to_bootstrap_);
}

void
galera::ReplicatorSMM::abort()
{
    log_info << "ReplicatorSMM::abort()";
    gcs_.close();
    gu_abort();
}
