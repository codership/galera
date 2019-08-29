//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
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


#define TX_SET_STATE(t_,s_) (t_).set_state(s_, __LINE__)


wsrep_cap_t
galera::ReplicatorSMM::capabilities(int protocol_version)
{
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

    static uint64_t const v8_caps(WSREP_CAP_STREAMING);

    static uint64_t const v9_caps(WSREP_CAP_NBO);

    if (protocol_version == -1) return 0;

    assert(protocol_version >= 4);

    uint64_t caps(v4_caps);

    if (protocol_version >= 5) caps |= v5_caps;
    if (protocol_version >= 8) caps |= v8_caps;
    if (protocol_version >= 9) caps |= v9_caps;

    return caps;
}


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
                         TrxHandleMaster::Defaults.record_set_ver_,
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

    if (seqno >= 0) // non-trivial starting position
    {
        assert(uuid != WSREP_UUID_UNDEFINED);
        cc_seqno_ = seqno; // is it needed here?

        log_debug << "ReplicatorSMM() initial position: "
                  << uuid << ':' << seqno;
        set_initial_position(uuid, seqno);
        cert_.assign_initial_position(gu::GTID(uuid, seqno),
                                      trx_params_.version_);
        gcache_.seqno_reset(gu::GTID(uuid, seqno));
        // update gcache position to one supplied by app.
    }

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

    if (state_uuid_ != WSREP_UUID_UNDEFINED)
    {
        st_.set (state_uuid_, last_committed(), safe_to_bootstrap_);
    }

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
    wsrep_seqno_t const seqno(last_committed());
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

        GU_DBUG_SYNC_EXECUTE("before_async_recv_process_sync", sleep(5););

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

            if (WSREP_OK == retval)
            {
                log_warn << "Broken shutdown sequence, provider state: "
                         << state_() << ", retval: " << retval;
                assert (0);
            }

            start_closing();

            // Generate zero view before exit to notify application
            gcs_act_cchange const cc;
            wsrep_uuid_t tmp(uuid_);
            wsrep_view_info_t* const err_view
                (galera_view_info_create(cc, capabilities(cc.repl_proto_ver),
                                         -1, tmp));
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
    assert(!ts.is_committed());
    if (!ts.skip_event())
    {
        assert(ts.trx_id() != uint64_t(-1) || ts.is_toi());
        assert(ts.certified() /*Repl*/ || ts.preordered() /*IST*/);
        assert(ts.local() == false || ts.nbo_end() ||
               (ts.flags() & TrxHandle::F_ROLLBACK));
        assert(ts.nbo_end() == false || ts.is_dummy());
    }

    ApplyException ae;

    ApplyOrder ao(ts);
    CommitOrder co(ts, co_mode_);

    if (gu_likely(TrxHandle::S_ABORTING != ts.state()))
        TX_SET_STATE(ts, TrxHandle::S_APPLYING);

    gu_trace(apply_monitor_.enter(ao));

    if (gu_unlikely(ts.nbo_start() == true))
    {
        // Non-blocking operation start, mark state unsafe.
        st_.mark_unsafe();
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

        if (!st_.corrupt())
        {
            assert(0 == e.data_len());
            /* non-empty error must be handled in handle_apply_error(), while
             * still in commit monitor. */
            on_inconsistency();
        }
    }
    /* at this point any other exception is fatal, not catching anything else.*/

    if (ts.local() == false)
    {
        GU_DBUG_SYNC_WAIT("after_commit_slave_sync");
    }

    wsrep_seqno_t const safe_to_discard(cert_.set_trx_committed(ts));

    /* For now need to keep it inside apply monitor to ensure all processing
     * ends by the time monitors are drained because of potential gcache
     * cleanup (and loss of the writeset buffer). Perhaps unordered monitor
     * is needed here. */
    ts.unordered(recv_ctx, unordered_cb_);

    apply_monitor_.leave(ao);

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
    assert(!(trx.flags() & TrxHandle::F_ROLLBACK));
    assert(trx.state() == TrxHandle::S_EXECUTING ||
           trx.state() == TrxHandle::S_MUST_ABORT);

    if (state_() < S_JOINED || trx.state() == TrxHandle::S_MUST_ABORT)
    {
    must_abort:
        if (trx.state() == TrxHandle::S_EXECUTING ||
            trx.state() == TrxHandle::S_REPLICATING)
            TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);

        TX_SET_STATE(trx, TrxHandle::S_ABORTING);

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
    TX_SET_STATE(trx, TrxHandle::S_REPLICATING);

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
            TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
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
    if (gu_unlikely(ts->flags() & TrxHandle::F_ROLLBACK))
    {
        // ROLLBACK fragments should be replicated through ReplicatorSMM::send(),
        // assert here for debug builds to catch if this is not a case.
        assert(0);
        assert(ts->depends_seqno() > 0); // must be set at unserialization
        ts->cert_bypass(true);
        ts->mark_certified();

        TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
        TX_SET_STATE(trx, TrxHandle::S_ABORTING);
        // to pass asserts in post_rollback
        TX_SET_STATE(*ts, TrxHandle::S_ABORTING);

        pending_cert_queue_.push(ts);
        cancel_monitors_for_local(*ts);

        goto out;
    }

    if (gu_unlikely(trx.state() == TrxHandle::S_MUST_ABORT))
    {
        retval = cert_for_aborted(ts);

        if (retval != WSREP_BF_ABORT)
        {
            assert(trx.state() == TrxHandle::S_MUST_ABORT);
            TX_SET_STATE(trx, TrxHandle::S_ABORTING);

            pending_cert_queue_.push(ts);
            cancel_monitors_for_local(*ts);

            assert(ts->is_dummy());
            assert(WSREP_OK != retval);
        }
        else
        {
            // If the transaction was committing, it must replay.
            if (ts->flags() & TrxHandle::F_COMMIT)
            {
                TX_SET_STATE(trx, TrxHandle::S_MUST_REPLAY);
            }
            else
            {
                TX_SET_STATE(*ts, TrxHandle::S_ABORTING);
                TX_SET_STATE(trx, TrxHandle::S_ABORTING);

                pending_cert_queue_.push(ts);
                cancel_monitors_for_local(*ts);

                retval = WSREP_TRX_FAIL;
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
galera::ReplicatorSMM::abort_trx(TrxHandleMaster& trx, wsrep_seqno_t bf_seqno,
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
    else
    {
        log_debug << "aborting trx " << trx;
    }

    wsrep_status_t retval(WSREP_OK);
    switch (trx.state())
    {
    case TrxHandle::S_MUST_ABORT:
    case TrxHandle::S_ABORTING:
    case TrxHandle::S_MUST_REPLAY:
        // victim trx was already BF aborted or it failed certification
        retval = WSREP_NOT_ALLOWED;
        break;
    case TrxHandle::S_EXECUTING:
        TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
        break;
    case TrxHandle::S_REPLICATING:
    {
        // @note: it is important to place set_state() into beginning of
        // every case, because state must be changed AFTER switch() and
        // BEFORE entering monitors or taking any other action.
        TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
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
        TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
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
        TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
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
            if (interrupted || !(ts->flags() & TrxHandle::F_COMMIT))
            {
                TX_SET_STATE(trx, TrxHandle::S_MUST_ABORT);
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
        *victim_seqno = (ts != 0 ? ts->global_seqno() : WSREP_SEQNO_UNDEFINED);
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
            assert(trx.state() == TrxHandle::S_MUST_REPLAY ||
                   !(ts->flags() & TrxHandle::F_COMMIT));
            assert(ts->state() == TrxHandle::S_REPLICATING ||
                   ts->state() == TrxHandle::S_CERTIFYING ||
                   ts->state() == TrxHandle::S_ABORTING);
            // apply monitor will be entered in due course during replay
            break;
        case WSREP_TRX_FAIL:
            /* committing fragment fails certification or non-committing BF'ed */
            assert(ts->depends_seqno() < 0 ||
                   (ts->flags() & TrxHandle::F_COMMIT) == 0);
            assert(ts->state() == TrxHandle::S_ABORTING);
            // trx will rollback, must enter apply monitor without blocking
            apply_monitor_enter_immediately(*ts.get());
            break;
        default:
            assert(0);
        }

        return retval;
    }

    assert(ts->global_seqno() > last_committed());
    assert(ts->depends_seqno() >= 0);

    TX_SET_STATE(trx, TrxHandle::S_APPLYING);

    ApplyOrder ao(*ts);
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
            TX_SET_STATE(trx, TrxHandle::S_MUST_REPLAY);

            if (!interrupted)
                TX_SET_STATE(*ts, TrxHandle::S_APPLYING);
            else
                assert(ts->state() == TrxHandle::S_CERTIFYING);
        }
        else
        {
            TX_SET_STATE(*ts, TrxHandle::S_ABORTING);
            TX_SET_STATE(trx, TrxHandle::S_ABORTING);

            if (interrupted == true)
            {
                assert(!apply_monitor_.entered(ao));
                apply_monitor_enter_immediately(*ts);
            }
            else
            {
                assert(apply_monitor_.entered(ao));
            }
        }
        retval = WSREP_BF_ABORT;
    }
    else
    {
        assert(apply_monitor_.entered(ao));
        TX_SET_STATE(*ts, TrxHandle::S_APPLYING);
    }

    assert(trx.state() != TrxHandle::S_MUST_ABORT);

    assert((retval == WSREP_OK && (trx.state() == TrxHandle::S_APPLYING ||
                                   trx.state() == TrxHandle::S_EXECUTING))
           ||
           (retval == WSREP_BF_ABORT && (
               trx.state() == TrxHandle::S_MUST_REPLAY ||
               trx.state() == TrxHandle::S_ABORTING))
        );

    if (meta) meta->depends_on = ts->depends_seqno();

    return retval;
}


wsrep_status_t galera::ReplicatorSMM::replay_trx(TrxHandleMaster& trx,
                                                 TrxHandleLock& lock,
                                                 void* const      trx_ctx)
{
    TrxHandleSlavePtr tsp(trx.ts());
    assert(tsp);
    TrxHandleSlave& ts(*tsp);

    assert(ts.global_seqno() > last_committed());

    log_debug << "replay trx: " << trx << " ts: " << ts;

    if (trx.state() == TrxHandle::S_MUST_ABORT)
    {
        /* Aborted after certify() returned (meaning apply monitor entered) */
#ifndef NDEBUG
        ApplyOrder ao(ts);
        assert(apply_monitor_.entered(ao));
#endif
        TX_SET_STATE(trx, TrxHandle::S_MUST_REPLAY);
    }

    assert(trx.state() == TrxHandle::S_MUST_REPLAY);
    assert(trx.trx_id() != static_cast<wsrep_trx_id_t>(-1));

    wsrep_status_t retval(WSREP_OK);

    // Note: We set submit NULL trx pointer below to avoid
    // interrupting replaying in any monitor during replay.

    switch (ts.state())
    {
    case TrxHandle::S_REPLICATING:
        retval = cert_and_catch(&trx, tsp);
        if (retval != WSREP_OK)
        {
            assert(retval == WSREP_TRX_FAIL);
            assert(ts.state() == TrxHandle::S_ABORTING);
            apply_monitor_enter_immediately(ts);
            break;
        }
        assert(ts.state() == TrxHandle::S_CERTIFYING);
        // fall through
    case TrxHandle::S_CERTIFYING:
    {
        assert(ts.state() == TrxHandle::S_CERTIFYING);
        // safety measure to make sure that all preceding trxs finish before
        // replaying
        wsrep_seqno_t const ds(ts.depends_seqno());
        ts.set_depends_seqno(ts.global_seqno() - 1);

        ApplyOrder ao(ts);
        assert(apply_monitor_.entered(ao) == false);
        gu_trace(apply_monitor_.enter(ao));

        // restore dependency info
        ts.set_depends_seqno(WSREP_SEQNO_UNDEFINED);
        ts.set_depends_seqno(ds);
        TX_SET_STATE(ts, TrxHandle::S_APPLYING);
    }
    // fall through
    case TrxHandle::S_APPLYING:
        //
        // Commit monitor will be entered from commit_order_enter_remote.
        //
        // fall through
    case TrxHandle::S_COMMITTING:
        ++local_replays_;

        TX_SET_STATE(trx, TrxHandle::S_REPLAYING);
        try
        {
            // Only committing transactions should be replayed
            assert(ts.flags() & TrxHandle::F_COMMIT);

            wsrep_trx_meta_t meta = {{ state_uuid_,    ts.global_seqno() },
                                     { ts.source_id(), ts.trx_id(),
                                       ts.conn_id()                      },
                                     ts.depends_seqno()};

            /* failure to replay own trx is certainly a sign of inconsistency,
             * not trying to catch anything here */
            assert(trx.owned());
            bool unused(false);
            lock.unlock();
            gu_trace(ts.apply(trx_ctx, apply_cb_, meta, unused));
            lock.lock();
            assert(false == unused);
            log_debug << "replayed " << ts.global_seqno();
            assert(ts.state() == TrxHandle::S_COMMITTED);
            assert(trx.state() == TrxHandle::S_COMMITTED);
        }
        catch (gu::Exception& e)
        {
            on_inconsistency();
            return WSREP_NODE_FAIL;
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
galera::ReplicatorSMM::handle_commit_interrupt(TrxHandleMaster& trx,
                                               const TrxHandleSlave& ts)
{
    assert(trx.state() == TrxHandle::S_MUST_ABORT);

    if (ts.flags() & TrxHandle::F_COMMIT)
    {
        TX_SET_STATE(trx, TrxHandle::S_MUST_REPLAY);
        return WSREP_BF_ABORT;
    }
    else
    {
        TX_SET_STATE(trx, TrxHandle::S_ABORTING);
        return WSREP_TRX_FAIL;
    }
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_enter_local(TrxHandleMaster& trx)
{
    assert(trx.local());
    assert(trx.ts() && trx.ts()->global_seqno() > 0);
    assert(trx.locked());

    assert(trx.state() == TrxHandle::S_APPLYING  ||
           trx.state() == TrxHandle::S_ABORTING  ||
           trx.state() == TrxHandle::S_REPLAYING);

    TrxHandle::State const next_state
        (trx.state() == TrxHandle::S_ABORTING ?
         TrxHandle::S_ROLLING_BACK : TrxHandle::S_COMMITTING);

    TX_SET_STATE(trx, next_state);

    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        TrxHandleSlavePtr tsp(trx.ts());
        TrxHandleSlave& ts(*tsp);

#ifndef NDEBUG
        {
            ApplyOrder ao(ts);
            assert(apply_monitor_.entered(ao));
        }
#endif

        CommitOrder co(ts, co_mode_);
        if (ts.state() < TrxHandle::S_COMMITTING)
        {
            assert(!commit_monitor_.entered(co));
        }
        else
        {
            /* was BF'ed after having entered commit monitor */
            assert(commit_monitor_.entered(co));
            return WSREP_OK;
        }

        try
        {
            trx.unlock();
            GU_DBUG_SYNC_WAIT("before_local_commit_monitor_enter");
            gu_trace(commit_monitor_.enter(co));
            assert(commit_monitor_.entered(co));
            trx.lock();

            TX_SET_STATE(ts, ts.state() == TrxHandle::S_APPLYING ?
                         TrxHandle::S_COMMITTING : TrxHandle::S_ROLLING_BACK);

            /* non-committing fragments may be interrupted after having entered
             * commit_monitor_ */
            if (0 == (ts.flags() & TrxHandle::F_COMMIT) &&
                trx.state() == TrxHandle::S_MUST_ABORT)
                return handle_commit_interrupt(trx, ts);

            assert(trx.state() == TrxHandle::S_COMMITTING ||
                   trx.state() == TrxHandle::S_ROLLING_BACK);

        }
        catch (gu::Exception& e)
        {
            assert(!commit_monitor_.entered(co));
            assert(next_state != TrxHandle::S_ROLLING_BACK);
            trx.lock();
            if (e.get_errno() == EINTR)
            {
                return handle_commit_interrupt(trx, ts);
            }
            else throw;
        }
        assert(ts.global_seqno() > last_committed());
    }
    assert(trx.locked());

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_ROLLING_BACK);

    return WSREP_OK;
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_enter_remote(TrxHandleSlave& trx)
{
    assert(trx.global_seqno() > 0);
    assert(trx.state() == TrxHandle::S_APPLYING  ||
           trx.state() == TrxHandle::S_ABORTING);

#ifndef NDEBUG
    if (trx.state() == TrxHandle::S_REPLAYING)
    {
        assert(trx.local());
        assert((trx.flags() & TrxHandle::F_ROLLBACK) == 0);

        ApplyOrder ao(trx);
        assert(apply_monitor_.entered(ao));
    }
#endif /* NDEBUG */

    CommitOrder co(trx, co_mode_);

    assert(!commit_monitor_.entered(co));

    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        gu_trace(commit_monitor_.enter(co));
    }

    TX_SET_STATE(trx, trx.state() == TrxHandle::S_ABORTING ?
                  TrxHandle::S_ROLLING_BACK : TrxHandle::S_COMMITTING);

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_ROLLING_BACK);

    return WSREP_OK;
}

void galera::ReplicatorSMM::process_apply_error(TrxHandleSlave& trx,
                                                const wsrep_buf_t& error)
{
    gu::GTID const gtid(state_uuid_, trx.global_seqno());
    int res;

    if (trx.local_seqno() != -1 || trx.nbo_end())
    {
        /* this must be done IN ORDER to avoid multiple elections, hence
         * anything else but LOCAL_OOOC and NO_OOOC is potentially broken */
        res = gcs_.vote(gtid, -1, error.ptr, error.len);
    }
    else res = 2;

    if (res != 0)
    {
        std::ostringstream os;

        switch (res)
        {
        case 2:
            os << "Failed on preordered " << gtid << ": inconsistency.";
            break;
        case 1:
            os << "Inconsistent by consensus on " << gtid;
            break;
        default:
            os << "Could not reach consensus on " << gtid
               << " (rcode: " << res << "), assuming inconsistency.";
        }

        galera::ApplyException ae(os.str(), NULL, error.ptr, error.len);
        GU_TRACE(ae);
        throw ae;
    }
    else
    {
        /* mark action as invalid (skip seqno) and return normally */
        gcache_.seqno_skip(trx.action().first,
                           trx.global_seqno(), GCS_ACT_WRITESET);
    }
}

wsrep_status_t
galera::ReplicatorSMM::handle_apply_error(TrxHandleSlave&    ts,
                                          const wsrep_buf_t& error,
                                          const std::string& custom_msg)
{
    assert(error.len > 0);

    std::ostringstream os;

    os << custom_msg << ts.global_seqno() << ", error: ";
    dump_buf(os, error.ptr, error.len);
    log_debug << "handle_apply_error(): " << os.str();

    try
    {
        if (!st_.corrupt())
            gu_trace(process_apply_error(ts, error));
        return WSREP_OK;
    }
    catch (ApplyException& e)
    {
        log_error << "Inconsistency detected: " << e.what();
        on_inconsistency();
    }
    catch (gu::Exception& e)
    {
        log_error << "Unexpected exception: " << e.what();
            assert(0);
            abort();
    }
    catch (...)
    {
        log_error << "Unknown exception";
        assert(0);
        abort();
    }

    return WSREP_NODE_FAIL;
}

wsrep_status_t
galera::ReplicatorSMM::commit_order_leave(TrxHandleSlave&          trx,
                                          const wsrep_buf_t* const error)
{
    if (trx.state() == TrxHandle::S_MUST_ABORT &&
        (trx.flags() & TrxHandle::F_COMMIT))
    {
        assert(0);
        // This is possible in case of ALG: BF applier BF aborts
        // trx that has already grabbed commit monitor and is committing.
        // However, this should be acceptable assuming that commit
        // operation does not reserve any more resources and is able
        // to release already reserved resources.
        log_debug << "trx was BF aborted during commit: " << trx;
        // manipulate state to avoid crash
        TX_SET_STATE(trx, TrxHandle::S_MUST_REPLAY);
        TX_SET_STATE(trx, TrxHandle::S_REPLAYING);
    }

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_REPLAYING  ||
           trx.state() == TrxHandle::S_ABORTING   ||
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
        end_state = TrxHandle::S_ROLLED_BACK;
        retval = handle_apply_error(trx, *error, "Failed to apply writeset ");
    }

    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        CommitOrder co(trx, co_mode_);
        commit_monitor_.leave(co);
    }

    TX_SET_STATE(trx, end_state);
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
    // Streaming transaction may enter here in aborting state if the
    // BF abort happens during fragment commit ordering. Otherwise
    // should always be committed.
    assert(trx.state() == TrxHandle::S_COMMITTED ||
           (trx.state() == TrxHandle::S_ABORTING &&
            (ts.flags() & TrxHandle::F_COMMIT) == 0));
    assert(!ts.is_committed());

    wsrep_seqno_t const safe_to_discard(cert_.set_trx_committed(ts));

    ApplyOrder ao(ts);
    apply_monitor_.leave(ao);

    if ((ts.flags() & TrxHandle::F_COMMIT) == 0 &&
        trx.state() == TrxHandle::S_COMMITTED)
    {
        // continue streaming
        TX_SET_STATE(trx, TrxHandle::S_EXECUTING);
    }

    trx.reset_ts();

    ++local_commits_;

    report_last_committed(safe_to_discard);

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::release_rollback(TrxHandleMaster& trx)
{
    assert(trx.locked());

    if (trx.state() == TrxHandle::S_MUST_ABORT) // BF abort before replicaiton
        TX_SET_STATE(trx, TrxHandle::S_ABORTING);

    if (trx.state() == TrxHandle::S_ABORTING ||
        trx.state() == TrxHandle::S_EXECUTING)
        TX_SET_STATE(trx, TrxHandle::S_ROLLED_BACK);

    assert(trx.state() == TrxHandle::S_ROLLED_BACK);

    TrxHandleSlavePtr tsp(trx.ts());
    if (tsp)
    {
        TrxHandleSlave& ts(*tsp);

        log_debug << "release_rollback() trx: " << trx << ", ts: " << ts;

        if (ts.global_seqno() > 0)
        {
            // ts.depends_seqno() may be invalid here
            ApplyOrder ao(ts.global_seqno(), 0, ts.local());
            assert(apply_monitor_.entered(ao));

            if (ts.state() < TrxHandle::S_COMMITTED)
            {
                CommitOrder co(ts, co_mode_);

                if (ts.state() < TrxHandle::S_COMMITTING)
                {
                    assert(!commit_monitor_.entered(co));
                    commit_monitor_.enter(co);
                }

                assert(commit_monitor_.entered(co));
                commit_monitor_.leave(co);
            } //committed
            else
            {
                assert(trx.state() == TrxHandle::S_ROLLED_BACK);
                assert(ts.state() == TrxHandle::S_ROLLED_BACK ||
                       ts.state() == TrxHandle::S_COMMITTED);
                assert(ts.global_seqno() <= commit_monitor_.last_left());
            }

            assert(commit_monitor_.last_left() >= ts.global_seqno());

            /* Queued transactions will be set committed in the queue */
            wsrep_seqno_t const safe_to_discard
                (ts.queued() ?
                 WSREP_SEQNO_UNDEFINED : cert_.set_trx_committed(ts));

            apply_monitor_.leave(ao);
            report_last_committed(safe_to_discard);
        }
        else
        {
            assert(0); // remove this if()
        }
    }
    else
    {
        log_debug << "release_rollback() trx: " << trx << ", ts: nil";
    }

    // Trx was either rolled back by user or via certification failure,
    // last committed report not needed since cert index state didn't change.
    // report_last_committed();

    trx.reset_ts();
    ++local_rollbacks_;

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::sync_wait(wsrep_gtid_t* upto,
                                                int           tout,
                                                wsrep_gtid_t* gtid)
{
    gu::GTID wait_gtid;
    gu::datetime::Date wait_until(gu::datetime::Date::calendar() +
                                  ((tout == -1) ?
                                   gu::datetime::Period(causal_read_timeout_) :
                                   gu::datetime::Period(tout * gu::datetime::Sec)));

    if (upto == 0)
    {
        try
        {
            gcs_.caused(wait_gtid, wait_until);
        }
        catch (gu::Exception& e)
        {
            log_warn << "gcs_caused() returned " << -e.get_errno()
                     << " (" << strerror(e.get_errno()) << ")";
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

        // Note: Since wsrep API 26 application may request release of
        // commit monitor before the commit actually happens (commit
        // may have been ordered/queued on application side for later
        // processing). Therefore we now rely on apply_monitor on sync
        // wait. This is sufficient since apply_monitor is always released
        // only after the whole transaction is over.
        apply_monitor_.wait(wait_gtid, wait_until);

        if (gtid != 0)
        {
            (void)last_committed_id(gtid);
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


wsrep_status_t galera::ReplicatorSMM::last_committed_id(wsrep_gtid_t* gtid) const
{
    // Note that we need to use apply monitor to determine last committed
    // here. Due to group commit implementation, the commit monitor may
    // be released before the commit has finished and the changes
    // made by the transaction have become visible. Therefore we rely
    // on apply monitor since it remains grabbed until the whole
    // commit is over.
    apply_monitor_.last_left_gtid(*gtid);
    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::wait_nbo_end(TrxHandleMaster* trx,
                                                   wsrep_trx_meta_t* meta)
{
    gu::shared_ptr<NBOCtx>::type
        nbo_ctx(cert_.nbo_ctx(meta->gtid.seqno));

    // Send end message
    trx->set_state(TrxHandle::S_REPLICATING);

    WriteSetNG::GatherVector actv;
    size_t const actv_size(
        trx->write_set_out().gather(trx->source_id(),
                                    trx->conn_id(),
                                    trx->trx_id(),
                                    actv));
  resend:
    wsrep_seqno_t lc(last_committed());
    if (lc == WSREP_SEQNO_UNDEFINED)
    {
        // Provider has been closed
        return WSREP_NODE_FAIL;
    }
    trx->finalize(lc);

    trx->unlock();
    int err(gcs_.sendv(actv, actv_size, GCS_ACT_WRITESET, false, false));
    trx->lock();

    if (err == -EAGAIN || err == -ENOTCONN || err == -EINTR)
    {
        // Send was either interrupted due to states excahnge (EAGAIN),
        // due to non-prim (ENOTCONN) or due to timeout in send monitor
        // (EINTR).
        return WSREP_CONN_FAIL;
    }
    else if (err < 0)
    {
        log_error << "Failed to send NBO-end: " << err << ": "
                  << ::strerror(-err);
        return WSREP_NODE_FAIL;
    }

    TrxHandleSlavePtr end_ts;
    while ((end_ts = nbo_ctx->wait_ts()) == 0)
    {
        if (closing_ || state_() == S_CLOSED)
        {
            log_error << "Closing during nonblocking operation. "
                "Node will be left in inconsistent state and must be "
                "re-initialized either by full SST or from backup.";
            return WSREP_FATAL;
        }

        if (nbo_ctx->aborted())
        {
            log_debug << "NBO wait aborted, retrying send";
            // Wait was aborted by view change, resend message
            goto resend;
        }
    }

    assert(end_ts->ends_nbo() != WSREP_SEQNO_UNDEFINED);

    trx->add_replicated(end_ts);

    meta->gtid.uuid  = state_uuid_;
    meta->gtid.seqno = end_ts->global_seqno();
    meta->depends_on = end_ts->depends_seqno();

    ApplyOrder ao(*end_ts);
    apply_monitor_.enter(ao);

    CommitOrder co(*end_ts, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS)
    {
        commit_monitor_.enter(co);
    }
    end_ts->set_state(TrxHandle::S_APPLYING);
    end_ts->set_state(TrxHandle::S_COMMITTING);

    trx->set_state(TrxHandle::S_CERTIFYING);
    trx->set_state(TrxHandle::S_APPLYING);
    trx->set_state(TrxHandle::S_COMMITTING);

    // Unref
    cert_.erase_nbo_ctx(end_ts->ends_nbo());

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::to_isolation_begin(TrxHandleMaster&  trx,
                                                         wsrep_trx_meta_t* meta)
{
    assert(trx.locked());

    if (trx.nbo_end())
    {
        return wait_nbo_end(&trx, meta);
    }

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
    assert(ts.global_seqno() > last_committed());

    CommitOrder co(ts, co_mode_);
    wsrep_status_t const retval(cert_and_catch(&trx, ts_ptr));
#if 0
    if (ts.nbo_start() == true || ts.nbo_end() == true)
    {
        log_info << "\n     MASTER processing NBO_"
                 << (ts.nbo_start() ? "START(" : "END(")
                 << ts.global_seqno() << ")"
                 << (WSREP_OK == retval ? ", must apply" : ", skip")
                 << ", ends NBO: " << ts.ends_nbo();
    }
#endif
    switch (retval)
    {
    case WSREP_OK:
    {
        TX_SET_STATE(trx, TrxHandle::S_APPLYING);
        TX_SET_STATE(ts, TrxHandle::S_APPLYING);

        ApplyOrder ao(ts);
        gu_trace(apply_monitor_.enter(ao));

        TX_SET_STATE(trx, TrxHandle::S_COMMITTING);
        TX_SET_STATE(ts, TrxHandle::S_COMMITTING);
        break;
    }
    case WSREP_TRX_FAIL:
        assert(ts.state() == TrxHandle::S_ABORTING);
        apply_monitor_enter_immediately(ts);
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

    log_debug << "Done executing TO isolated action: " << ts;

    assert(trx.state() == TrxHandle::S_COMMITTING ||
           trx.state() == TrxHandle::S_ABORTING);
    assert(ts.state() == TrxHandle::S_COMMITTING ||
           ts.state() == TrxHandle::S_ABORTING);

    wsrep_status_t ret(WSREP_OK);
    if (NULL != err && NULL != err->ptr)
    {
        ret = handle_apply_error(ts, *err, "Failed to execute TOI action ");
    }

    CommitOrder co(ts, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.leave(co);

    wsrep_seqno_t const safe_to_discard(cert_.set_trx_committed(ts));

    ApplyOrder ao(ts);
    apply_monitor_.leave(ao);

    if (ts.state() == TrxHandle::S_COMMITTING)
    {
        assert(trx.state() == TrxHandle::S_COMMITTING);
        TX_SET_STATE(trx, TrxHandle::S_COMMITTED);
        TX_SET_STATE(ts, TrxHandle::S_COMMITTED);

        if (trx.nbo_start() == false) st_.mark_safe();
    }
    else
    {
        assert(trx.state() == TrxHandle::S_ABORTING);
        assert(ts.state() == TrxHandle::S_ABORTING);
        TX_SET_STATE(trx, TrxHandle::S_ROLLED_BACK);
        TX_SET_STATE(ts, TrxHandle::S_ROLLING_BACK);
        TX_SET_STATE(ts, TrxHandle::S_ROLLED_BACK);
    }

    report_last_committed(safe_to_discard);

    return ret;
}


namespace galera
{

static WriteSetOut*
writeset_from_handle (wsrep_po_handle_t&             handle,
                      const TrxHandleMaster::Params& trx_params)
{
    WriteSetOut* ret = static_cast<WriteSetOut*>(handle.opaque);

    if (NULL == ret)
    {
        try
        {
            ret = new WriteSetOut(
//                gu::String<256>(trx_params.working_dir_) << '/' << &handle,
                trx_params.working_dir_, wsrep_trx_id_t(&handle),
                /* key format is not essential since we're not adding keys */
                KeySet::version(trx_params.key_format_), NULL, 0, 0,
                trx_params.record_set_ver_,
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
#if 0
    if (ts.nbo_start() == true || ts.nbo_end() == true)
    {
        log_info << "\n     SLAVE processing NBO_"
                 << (ts.nbo_start() ? "START(" : "END(")
                 << ts.global_seqno() << ")"
                 << (WSREP_OK == retval ? ", must apply" : ", skip")
                 << ", ends NBO: " << ts.ends_nbo();
    }
#endif
    switch (retval)
    {
    case WSREP_TRX_FAIL:
        assert(ts.state() == TrxHandle::S_ABORTING);
        /* fall through to apply_trx() */
    case WSREP_OK:
        try
        {
            if (ts.nbo_end() == true)
            {
                // NBO-end events are for internal operation only, not to be
                // consumed by application. If the NBO end happens with
                // different seqno than the current event's global seqno,
                // release monitors. In other case monitors will be grabbed
                // by local NBO handler threads.
                if (ts.ends_nbo() == WSREP_SEQNO_UNDEFINED)
                {
                    assert(WSREP_OK != retval);
                    assert(ts.state() == TrxHandle::S_ABORTING);
                }
                else
                {
                    assert(WSREP_OK == retval);
                    assert(ts.ends_nbo() > 0);
                    // Signal NBO waiter here after leaving local ordering
                    // critical section.
                    gu::shared_ptr<NBOCtx>::type nbo_ctx(
                        cert_.nbo_ctx(ts.ends_nbo()));
                    assert(nbo_ctx != 0);
                    nbo_ctx->set_ts(ts_ptr);
                    break;
                }
            }

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


void galera::ReplicatorSMM::process_vote(wsrep_seqno_t const seqno_g,
                                         wsrep_seqno_t const seqno_l,
                                         int64_t       const code)
{
    assert(seqno_g > 0);
    assert(seqno_l > 0);

    std::ostringstream msg;

    LocalOrder lo(seqno_l);
    gu_trace(local_monitor_.enter(lo));

    gu::GTID const gtid(state_uuid_, seqno_g);

    if (code > 0)  /* vote request */
    {
        assert(GCS_VOTE_REQUEST == code);
        log_info << "Got vote request for seqno " << gtid; //remove
        /* make sure WS was either successfully applied or already voted */
        if (last_committed() < seqno_g) drain_monitors(seqno_g);
        if (st_.corrupt()) goto out;

        int const ret(gcs_.vote(gtid, 0, NULL, 0));

        switch (ret)
        {
        case 0:         /* majority agrees */
            log_info << "Vote 0 (success) on " << gtid
                     << " is consistent with group. Continue.";
            goto out;
        case -EALREADY: /* already voted */
            log_info << gtid << " already voted on. Continue.";
            goto out;
        case 1:         /* majority disagrees */
            msg << "Vote 0 (success) on " << gtid
                << " is inconsistent with group. Leaving cluster.";
            goto fail;
        default:        /* general error */
            assert(ret < 0);
            msg << "Failed to vote on request for " << gtid << ": "
                << -ret << " (" << ::strerror(-ret) << "). "
                "Assuming inconsistency";
            goto fail;
        }
    }
    else if (code < 0)
    {
        msg << "Got negative vote on successfully applied " << gtid;
    fail:
        log_error << msg.str();
        on_inconsistency();
    }
    else
    {
        /* seems we are in majority */
    }
out:
    local_monitor_.leave(lo);
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
    trx_params_.record_set_ver_ = gu::RecordSet::VER1;

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
        // Protocol upgrade to enforce 8-byte alignment in writesets and CCs
        trx_params_.version_ = 3;
        trx_params_.record_set_ver_ = gu::RecordSet::VER2;
        str_proto_ver_ = 2;
        break;
    case 9:
        // Protocol upgrade to enable support for semi-shared key type.
        trx_params_.version_ = 4;
        trx_params_.record_set_ver_ = gu::RecordSet::VER2;
        str_proto_ver_ = 2;
        break;
    case 10:
        // Protocol upgrade to enable support for:
        trx_params_.version_ = 5;// PA range preset in the writeset,
                                 // WSREP_KEY_UPDATE support (API v26)
        trx_params_.record_set_ver_ = gu::RecordSet::VER2;
        str_proto_ver_ = 3; // CC events in IST.
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

void galera::ReplicatorSMM::record_cc_seqnos(wsrep_seqno_t cc_seqno,
                                             const char* source)
{
    cc_seqno_ = cc_seqno;
    cc_lowest_trx_seqno_ = cert_.lowest_trx_seqno();
    log_info << "Lowest cert indnex boundary for CC from " << source
             << ": " << cc_lowest_trx_seqno_;;
    log_info << "Min available from gcache for CC from " << source
             << ": " << gcache_.seqno_min();
    // Lowest TRX must not have been released from gcache at this
    // point.
    assert(cc_lowest_trx_seqno_ >= gcache_.seqno_min());
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
galera::ReplicatorSMM::submit_view_info(void*                    recv_ctx,
                                        const wsrep_view_info_t* view_info)
{
    wsrep_cb_status_t const rcode
        (view_cb_(app_ctx_, recv_ctx, view_info, 0, 0));

    if (WSREP_CB_SUCCESS != rcode)
    {
        gu_throw_fatal << "View callback failed. "
            "This is unrecoverable, restart required.";
    }
}

void
galera::ReplicatorSMM::process_conf_change(void*                    recv_ctx,
                                           const struct gcs_action& cc)
{
    static int const ORDERED_CC = 10; /* repl protocol version which orders CC */
    assert(cc.seqno_l > -1);

    gcs_act_cchange const conf(cc.buf, cc.size);

    bool const from_IST(0 == cc.seqno_l);
    bool const ordered(conf.repl_proto_ver >= ORDERED_CC);

    log_info << "####### processing CC " << conf.seqno
             << (from_IST ? ", from IST" : ", local")
             << (ordered ? ", ordered" : ", unordered");

    LocalOrder lo(cc.seqno_l);

    if (!from_IST)
    {
        gu_trace(local_monitor_.enter(lo));
        gu_trace(process_pending_queue(cc.seqno_g));
    }

    wsrep_seqno_t const upto(cert_.position());
    if (upto >= last_committed())
    {
        log_debug << "Drain monitors from " << last_committed()
                  << " upto " << upto;
        gu_trace(drain_monitors(upto));
    }
    else
    {
        /* this may happen when processing self-leave CC after connection
         * closure due to inconsistency. */
        assert(st_.corrupt());
    }

    int const prev_protocol_version(protocol_version_);

    if (conf.conf_id >= 0) // Primary configuration
    {
        assert(!from_IST || conf.repl_proto_ver >= ORDERED_CC);
        establish_protocol_versions (conf.repl_proto_ver);
    }

    // if CC comes from IST uuid_ must be already defined
    assert(!from_IST       || WSREP_UUID_UNDEFINED != uuid_);
    // we must have either my_idx (passed in seqno_g) or uuid_ defined
    assert(cc.seqno_g >= 0 || WSREP_UUID_UNDEFINED != uuid_);

    wsrep_uuid_t new_uuid(uuid_);
    wsrep_view_info_t* const view_info
        (galera_view_info_create(conf,
                                 capabilities(conf.repl_proto_ver),
                                 (!from_IST ? cc.seqno_g : -1), new_uuid));

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
                std::ostringstream msg;

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

        // First view from the group or group uuid has changed,
        // call connected callback to notify application.
        if ((first_view || state_uuid_ != group_uuid) && connected_cb_)
        {
            wsrep_cb_status_t cret(connected_cb_(app_ctx_, view_info));
            if (cret != WSREP_CB_SUCCESS)
            {
                log_fatal << "Application returned error "
                          << cret
                          << " from connect callback, aborting";
                abort();
            }
        }

        if (conf.seqno != WSREP_SEQNO_UNDEFINED &&
            conf.seqno <= sst_seqno_)
        {
            assert(!from_IST);
            log_info << "####### skipping CC " << conf.seqno
                     << (from_IST ? ", from IST" : ", local");

            // applied already in SST/IST, skip
            gu_trace(local_monitor_.leave(lo));
            resume_recv();
            gcache_.free(const_cast<void*>(cc.buf));
            ::free(view_info);
            return;
        }
    } // !from_IST

    update_incoming_list(*view_info);

    bool const st_required
        (state_transfer_required(*view_info, my_state == GCS_NODE_STATE_PRIM));

    void*  app_req(0);
    size_t app_req_len(0);
#ifndef NDEBUG
    bool   app_waits_sst(false);
#endif

    if (st_required)
    {
        log_info << "State transfer required: "
                 << "\n\tGroup state: " << group_uuid << ":" << group_seqno
                 << "\n\tLocal state: " << state_uuid_<< ":" << last_committed();

        assert(!from_IST);

        if (S_CONNECTED != state_()) state_.shift_to(S_CONNECTED);

        wsrep_cb_status_t const rcode(sst_request_cb_(app_ctx_,
                                                      &app_req, &app_req_len));

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
        // if protocol version >= ORDERED_CC, first CC already carries seqno 1,
        // so it can't be less than 1. For older protocols it can be 0.
        assert(group_seqno >= (protocol_version_ >= ORDERED_CC));

        //
        // Starting from protocol_version_ 8 joiner's cert index is rebuilt
        // from IST.
        //
        // The reasons to reset cert index:
        // - Protocol version lower than ORDERED_CC (ALL)
        // - Protocol upgrade                       (ALL)
        // - State transfer will take a place       (JOINER)
        //
        bool index_reset(protocol_version_ < ORDERED_CC ||
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

            if (protocol_version_ < ORDERED_CC)
            {
                position.set(group_uuid, group_seqno);
            }
            else
            {
                position = gu::GTID();
            }

            /* 2 reasons for this here:
             * 1 - compatibility with protocols < ORDERED_CC
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

            // Note: Monitor release/cancel happens after view event has been
            // processed.
            log_info << "####### Setting monitor position to " << group_seqno;
            set_initial_position(group_uuid, group_seqno - 1);

            if (!from_IST)
            {
                /* CCs from IST already have seqno assigned and cert. position
                 * adjusted */
                if (protocol_version_ >= ORDERED_CC)
                {
                    gu_trace(gcache_.seqno_assign(cc.buf, conf.seqno,
                                                  GCS_ACT_CCHANGE, false));
                }
                else /* before protocol ver 10 conf changes are not ordered */
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

            st_.set(state_uuid_, WSREP_SEQNO_UNDEFINED, safe_to_bootstrap_);
        }
        else
        {
            assert(!from_IST);
        }

        // record CC related state seqnos, needed for IST on DONOR
        record_cc_seqnos(group_seqno, "group");
        // GCache must contain some actions, at least this CC
        assert(gcache_.seqno_min() > 0 || conf.repl_proto_ver < ORDERED_CC);

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
        assert(!from_IST);

        // reset sst_seqno_ every time we disconnct from PC
        sst_seqno_ = WSREP_SEQNO_UNDEFINED;

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
    assert(!from_IST || conf.seqno > 0);
    assert(!st_required || conf.seqno > 0);

    if (!from_IST /* A separate view from IST will be passed to ISTEventQueue */
        &&
        (!st_required /* in-order processing */
         ||
         conf.seqno < 0 /* non-primary configuration */))
    {
        try
        {
            submit_view_info(recv_ctx, view_info);
        }
        catch (gu::Exception& e)
        {
            log_fatal << e.what();
            abort();
        }
    }

    free(view_info);

    // Cancel monitors after view event has been processed by the
    // application. Otherwise last_committed_id() will return incorrect
    // value if called from view callback.
    // IST will release monitors after its view is processed
    if (ordered && !from_IST && !st_required && group_seqno > 0)
        cancel_seqno(group_seqno);

    if (!from_IST)
    {
        double foo, bar;
        size_t index_size;
        cert_.stats_get(foo, bar, index_size);
        local_monitor_.leave(lo);
        resume_recv();
    }

    if (conf.conf_id < 0 && conf.memb.size() == 0) {
        assert(!from_IST);
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

    wsrep_seqno_t const ret(last_committed());
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

/* process pending queue events scheduled before seqno */
void galera::ReplicatorSMM::process_pending_queue(wsrep_seqno_t seqno)
{
    // pending_cert_queue_ contains all writesets that:
    //   a) were BF aborted before being certified
    //   b) are not going to be replayed even though
    //      cert_for_aborted() returned TEST_OK for them
    //
    // Before certifying the current seqno, check if
    // pending_cert_queue contains any smaller seqno.
    // This avoids the certification index to diverge
    // across nodes.
    TrxHandleSlavePtr queued_ts;
    while ((queued_ts = pending_cert_queue_.must_cert_next(seqno)) != NULL)
    {
        log_debug << "must cert next " << seqno << " aborted ts " << *queued_ts;

        Certification::TestResult const result(cert_.append_trx(queued_ts));

        log_debug << "trx in pending cert queue certified, result: " << result;

        assert(!queued_ts->cert_bypass() ||
               Certification::TestResult::TEST_OK == result);

        bool const skip(Certification::TestResult::TEST_FAILED == result &&
                        !(queued_ts->cert_bypass()/* expl. ROLLBACK */));

        /* at this point we are still assigning seqno to buffer in order */
        gcache_.seqno_assign(queued_ts->action().first,
                             queued_ts->global_seqno(),
                             GCS_ACT_WRITESET, skip);

        cert_.set_trx_committed(*queued_ts);
    }
}

/* don't use this directly, use cert_and_catch() instead */
inline
wsrep_status_t galera::ReplicatorSMM::cert(TrxHandleMaster* trx,
                                           const TrxHandleSlavePtr& ts)
{
    assert(trx == 0 ||
           (trx->state() == TrxHandle::S_REPLICATING ||
            trx->state() == TrxHandle::S_MUST_REPLAY));
    assert(ts->state() == TrxHandle::S_REPLICATING);

    assert(ts->local_seqno()     != WSREP_SEQNO_UNDEFINED);
    assert(ts->global_seqno()    != WSREP_SEQNO_UNDEFINED);
    assert(ts->last_seen_seqno() >= 0);
    assert(ts->last_seen_seqno() < ts->global_seqno());

    LocalOrder lo(*ts);
    bool       interrupted(false);
    bool       in_replay(trx != 0 &&
                         trx->state() == TrxHandle::S_MUST_REPLAY);

    try
    {
        if (trx != 0)
        {
            if (in_replay == false) TX_SET_STATE(*trx, TrxHandle::S_CERTIFYING);
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
                trx->state() == TrxHandle::S_MUST_REPLAY));

        TX_SET_STATE(*ts, TrxHandle::S_CERTIFYING);
    }
    catch (gu::Exception& e)
    {
        if (trx != 0) trx->lock();
        if (e.get_errno() == EINTR) { interrupted = true; }
        else throw;
    }

    wsrep_status_t retval(WSREP_OK);
    bool const applicable(ts->global_seqno() > last_committed());
    assert(!ts->local() || applicable); // applicable can't be false for locals

    if (gu_unlikely (interrupted))
    {
        assert(trx != 0);
        retval = cert_for_aborted(ts);

        if (WSREP_TRX_FAIL != retval)
        {
            assert(ts->state() == TrxHandle::S_REPLICATING ||
                   ts->state() == TrxHandle::S_CERTIFYING);
            assert(WSREP_BF_ABORT == retval);
            assert(trx != 0);

            // If the transaction was committing, it must replay.
            if (ts->flags() & TrxHandle::F_COMMIT)
            {
                TX_SET_STATE(*trx, TrxHandle::S_MUST_REPLAY);
                return retval;
            }
            // if not - we need to rollback, so pretend that certification
            // failed, but still update cert index to match slaves
            else
            {
                pending_cert_queue_.push(ts);
                TX_SET_STATE(*ts, TrxHandle::S_ABORTING);

                retval = WSREP_TRX_FAIL;
            }
        }
        else
        {
            assert(WSREP_TRX_FAIL == retval);
            assert(WSREP_SEQNO_UNDEFINED == ts->depends_seqno());
            pending_cert_queue_.push(ts);
        }

        assert(WSREP_TRX_FAIL == retval);
        assert(TrxHandle::S_ABORTING == ts->state());

        TX_SET_STATE(*trx, TrxHandle::S_ABORTING);

        local_monitor_.self_cancel(lo);
    }
    else
    {
        assert(ts->state() == TrxHandle::S_CERTIFYING);

        gu_trace(process_pending_queue(ts->global_seqno()));

        switch (cert_.append_trx(ts))
        {
        case Certification::TEST_OK:
            // NBO_END should certify positively only if it ends NBO
            assert(ts->ends_nbo() > 0 || !ts->nbo_end());
            if (gu_likely(applicable))
            {
                if (trx != 0 && trx->state() == TrxHandle::S_MUST_ABORT)
                {
                    if (ts->flags() & TrxHandle::F_COMMIT)
                    {
                        TX_SET_STATE(*trx, TrxHandle::S_MUST_REPLAY);
                        // apply monitor will be entered during replay
                    }
                    else
                    {
                        // Abort the transaction if non-committing
                        // fragment was BF aborted during certification.
                        TX_SET_STATE(*trx, TrxHandle::S_ABORTING);
                        TX_SET_STATE(*ts, TrxHandle::S_ABORTING);
                        apply_monitor_enter_immediately(*ts);
                    }
                    retval = WSREP_BF_ABORT;
                }
                else
                {
                    retval = WSREP_OK;
                }
                assert(ts->depends_seqno() >= 0);
            }
            else
            {
                // this can happen after SST position has been submitted
                // but not all actions preceding SST initial position
                // have been processed
                if (trx != 0) TX_SET_STATE(*trx, TrxHandle::S_ABORTING);
                TX_SET_STATE(*ts, TrxHandle::S_ABORTING);
                retval = WSREP_TRX_MISSING;
            }
            break;
        case Certification::TEST_FAILED:
            if (ts->nbo_end()) assert(ts->ends_nbo() == WSREP_SEQNO_UNDEFINED);
            assert(ts->state() == TrxHandle::S_ABORTING );
            // This check is not valid anymore. NBO may reserve resource
            // access for longer period, which must cause certification
            // to fail for all operations until the operation is over.
            // if (gu_unlikely(trx->is_toi() && applicable)) //small sanity check
            // {
            // may happen on configuration change
            //     log_warn << "Certification failed for TO isolated action: "
            //<< *trx;
            // assert(0);
            // }

            local_cert_failures_ += ts->local();
            if (trx != 0) TX_SET_STATE(*trx, TrxHandle::S_ABORTING);
            retval = applicable ? WSREP_TRX_FAIL : WSREP_TRX_MISSING;
            break;
        }

        // at this point we are about to leave local_monitor_. Make sure
        // trx checksum was alright before that.
        ts->verify_checksum();

        // we must do seqno assignment 'in order' for std::map reasons,
        // so keeping it inside the monitor. NBO end should never be skipped.
        bool const skip(ts->depends_seqno() < 0 && !ts->nbo_end());
        gcache_.seqno_assign (ts->action().first, ts->global_seqno(),
                              GCS_ACT_WRITESET, skip);

        if (gu_unlikely(WSREP_TRX_MISSING == retval))
        {
            assert(!applicable);
            /* this trx will never go through application chain */
            report_last_committed(cert_.set_trx_committed(*ts));
        }

        local_monitor_.leave(lo);
    }

    assert(WSREP_OK == retval || WSREP_TRX_FAIL == retval ||
           WSREP_TRX_MISSING == retval || WSREP_BF_ABORT == retval);

    if (gu_unlikely(WSREP_TRX_FAIL == retval))
    {
        assert(ts->state() == TrxHandle::S_ABORTING);
    }
    else
    {
        assert(WSREP_OK != retval || ts->depends_seqno() >= 0);
        if (WSREP_OK != retval && ts->local())
        {
            log_debug << "#############" << "Skipped cancel_monitors(): retval: "
                      << retval << ", trx: " << trx << ", ts: " << *ts;
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
        assert(!ts->nbo_end()); // should never be skipped in seqno_assign()
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
        // Copy only non-nil terminated part of the source string
        // and terminate the string explicitly to silence a warning
        // generated by Wstringop-truncation
        char* str(const_cast<char*>(state_uuid_str_));
        strncpy(str, os.str().c_str(), sizeof(state_uuid_str_) - 1);
        str[sizeof(state_uuid_str_) - 1] = '\0';
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
