//
// Copyright (C) 2010-2012 Codership Oy <info@codership.com>
//

#include "galera_common.hpp"
#include "replicator_smm.hpp"
#include "galera_exception.hpp"
#include "uuid.hpp"

extern "C"
{
#include "galera_info.h"
}

#include <sstream>
#include <iostream>

#if 0 // DELETE
static void
apply_wscoll(void*                    recv_ctx,
             wsrep_apply_cb_t         apply_cb,
             const galera::TrxHandle& trx,
             const wsrep_trx_meta_t&  meta)
{
    const gu::byte_t* buf(trx.write_set_buffer().first);
    const size_t buf_len(trx.write_set_buffer().second);
    size_t offset(0);
    while (offset < buf_len)
    {
        // Skip key segment
        std::pair<size_t, size_t> k(
            galera::WriteSet::segment(buf, buf_len, offset));
        offset = k.first + k.second;
        // Data part
        std::pair<size_t, size_t> d(
            galera::WriteSet::segment(buf, buf_len, offset));
        offset = d.first + d.second;

        wsrep_status_t err = apply_cb (recv_ctx,
                                       buf + d.first,
                                       d.second,
                                       &meta);

        if (gu_unlikely(err != WSREP_OK))
        {
            const char* const err_str(galera::wsrep_status_str(err));
            std::ostringstream os;

            os << "Failed to apply app buffer: "
               << "seqno: "<< trx.global_seqno() << ", status: " << err_str;

            galera::ApplyException ae(os.str(), err);

            GU_TRACE(ae);

            throw ae;
        }
    }

    assert(offset == buf_len);

    return;
}
#endif // DELETE

static void
apply_trx_ws(void*                    recv_ctx,
             wsrep_apply_cb_t         apply_cb,
             wsrep_commit_cb_t        commit_cb,
             const galera::TrxHandle& trx,
             const wsrep_trx_meta_t&  meta)
{
    static const size_t max_apply_attempts(10);
    size_t attempts(1);
    do
    {
        try
        {
            if (trx.is_toi())
            {
                log_debug << "Executing TO isolated action: " << trx;
            }
            gu_trace(trx.apply(recv_ctx, apply_cb, meta));
            if (trx.is_toi())
            {
                log_debug << "Done executing TO isolated action: "
                         << trx.global_seqno();
            }
            break;
        }
        catch (galera::ApplyException& e)
        {
            if (trx.is_toi())
            {
                log_warn << "Ignoring error for TO isolated action: " << trx;
                break;
            }
            else
            {
                wsrep_status_t err = e.wsrep_status();

                if (WSREP_TRX_FAIL == err)
                {
                    int const rcode(commit_cb(recv_ctx, &meta, false));
                    if (WSREP_OK != rcode)
                    {
                        gu_throw_fatal << "Rollback failed. Trx: " << trx;
                    }

                    ++attempts;

                    if (attempts <= max_apply_attempts)
                    {
                        log_warn << e.what()
                                 << "\nRetrying " << attempts << "th time";
                    }
                }
                else
                {
                    GU_TRACE(e);
                    throw;
                }
            }
        }
    }
    while (attempts <= max_apply_attempts);

    if (gu_unlikely(attempts > max_apply_attempts))
    {
        std::ostringstream msg;

        msg << "Failed to apply trx " << trx.global_seqno() << " "
            << max_apply_attempts << " times";

        throw galera::ApplyException(msg.str(), WSREP_TRX_FAIL);
    }

    return;
}


std::ostream& galera::operator<<(std::ostream& os, ReplicatorSMM::State state)
{
    switch (state)
    {
    case ReplicatorSMM::S_DESTROYED: return (os << "DESTROYED");
    case ReplicatorSMM::S_CLOSED:    return (os << "CLOSED");
    case ReplicatorSMM::S_CLOSING:   return (os << "CLOSING");
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
    logger_             (reinterpret_cast<gu_log_cb_t>(args->logger_cb)),
    config_             (args->options),
    set_defaults_       (config_, defaults, args->node_address),
    trx_proto_ver_      (-1),
    str_proto_ver_      (-1),
    protocol_version_   (-1),
    proto_max_          (gu::from_string<int>(config_.get(Param::proto_max))),
    state_              (S_CLOSED),
    sst_state_          (SST_NONE),
    co_mode_            (CommitOrder::from_string(
                             config_.get(Param::commit_order))),
    data_dir_           (args->data_dir ? args->data_dir : ""),
    state_file_         (data_dir_.length() ?
                         data_dir_+'/'+GALERA_STATE_FILE : GALERA_STATE_FILE),
    st_                 (state_file_),
    uuid_               (WSREP_UUID_UNDEFINED),
    state_uuid_         (WSREP_UUID_UNDEFINED),
    state_uuid_str_     (),
    cc_seqno_           (WSREP_SEQNO_UNDEFINED),
    app_ctx_            (args->app_ctx),
    view_cb_            (args->view_handler_cb),
    apply_cb_           (args->apply_cb),
    commit_cb_          (args->commit_cb),
    sst_donate_cb_      (args->sst_donate_cb),
    synced_cb_          (args->synced_cb),
    sst_donor_          (),
    sst_uuid_           (WSREP_UUID_UNDEFINED),
    sst_seqno_          (WSREP_SEQNO_UNDEFINED),
    sst_mutex_          (),
    sst_cond_           (),
    sst_retry_sec_      (1),
    ist_sst_            (false),
    gcache_             (config_, data_dir_),
    gcs_                (config_, gcache_, proto_max_, args->proto_ver,
                         args->node_name, args->node_incoming),
    service_thd_        (gcs_, gcache_),
    as_                 (0),
    gcs_as_             (gcs_, *this, gcache_),
    ist_receiver_       (config_, args->node_address),
    ist_senders_        (gcs_, gcache_),
    wsdb_               (),
    cert_               (config_, service_thd_),
    local_monitor_      (),
    apply_monitor_      (),
    commit_monitor_     (),
    causal_read_timeout_(config_.get(Param::causal_read_timeout)),
    receivers_          (),
    replicated_         (),
    replicated_bytes_   (),
    local_commits_      (),
    local_rollbacks_    (),
    local_cert_failures_(),
    local_bf_aborts_    (),
    local_replays_      (),
    incoming_list_      (""),
    incoming_mutex_     (),
    wsrep_stats_        ()
{
    // @todo add guards (and perhaps actions)
    state_.add_transition(Transition(S_CLOSED,  S_DESTROYED));
    state_.add_transition(Transition(S_CLOSED,  S_CONNECTED));
    state_.add_transition(Transition(S_CLOSING, S_CLOSED));

    state_.add_transition(Transition(S_CONNECTED, S_CLOSING));
    state_.add_transition(Transition(S_CONNECTED, S_CONNECTED));
    state_.add_transition(Transition(S_CONNECTED, S_JOINING));
    // the following is possible only when bootstrapping new cluster
    // (trivial wsrep_cluster_address)
    state_.add_transition(Transition(S_CONNECTED, S_JOINED));
    // the following are possible on PC remerge
    state_.add_transition(Transition(S_CONNECTED, S_DONOR));
    state_.add_transition(Transition(S_CONNECTED, S_SYNCED));

    state_.add_transition(Transition(S_JOINING, S_CLOSING));
    // the following is possible if one non-prim conf follows another
    state_.add_transition(Transition(S_JOINING, S_CONNECTED));
    state_.add_transition(Transition(S_JOINING, S_JOINED));

    state_.add_transition(Transition(S_JOINED, S_CLOSING));
    state_.add_transition(Transition(S_JOINED, S_CONNECTED));
    state_.add_transition(Transition(S_JOINED, S_SYNCED));

    state_.add_transition(Transition(S_SYNCED, S_CLOSING));
    state_.add_transition(Transition(S_SYNCED, S_CONNECTED));
    state_.add_transition(Transition(S_SYNCED, S_DONOR));

    state_.add_transition(Transition(S_DONOR, S_CLOSING));
    state_.add_transition(Transition(S_DONOR, S_CONNECTED));
    state_.add_transition(Transition(S_DONOR, S_JOINED));

    local_monitor_.set_initial_position(0);

    wsrep_uuid_t  uuid;
    wsrep_seqno_t seqno;

    st_.get (uuid, seqno);

    if (0 != args->state_uuid &&
        *args->state_uuid != WSREP_UUID_UNDEFINED &&
        *args->state_uuid == uuid &&
        seqno == WSREP_SEQNO_UNDEFINED)
    {
        /* non-trivial recovery information provided on startup, and db is safe
         * so use recovered seqno value */
        seqno = args->state_seqno;
    }
    log_debug << "End state: " << uuid << ':' << seqno << " #################";
    update_state_uuid (uuid);

    cc_seqno_ = seqno; // is it needed here?
    apply_monitor_.set_initial_position(seqno);

    if (co_mode_ != CommitOrder::BYPASS)
        commit_monitor_.set_initial_position(seqno);

    cert_.assign_initial_position(seqno, trx_proto_ver_);

    build_stats_vars(wsrep_stats_);
}

galera::ReplicatorSMM::~ReplicatorSMM()
{
    log_info << "dtor state: " << state_();
    switch (state_())
    {
    case S_CONNECTED:
    case S_JOINING:
    case S_JOINED:
    case S_SYNCED:
    case S_DONOR:
        close();
    case S_CLOSING:
        // @todo wait that all users have left the building
    case S_CLOSED:
        ist_senders_.cancel();
        break;
    case S_DESTROYED:
        break;
    }
}


wsrep_status_t galera::ReplicatorSMM::connect(const std::string& cluster_name,
                                              const std::string& cluster_url,
                                              const std::string& state_donor,
                                              bool  const        bootstrap)
{
    sst_donor_ = state_donor;
    service_thd_.reset();

    ssize_t err;
    wsrep_status_t ret(WSREP_OK);
    wsrep_seqno_t const seqno(cert_.position());
    wsrep_uuid_t  const gcs_uuid(seqno < 0 ? WSREP_UUID_UNDEFINED :state_uuid_);

    log_info << "Setting initial position to " << gcs_uuid << ':' << seqno;

    if ((err = gcs_.set_initial_position(gcs_uuid, seqno)) != 0)
    {
        log_error << "gcs init failed:" << strerror(-err);
        ret = WSREP_NODE_FAIL;
    }

    gcache_.reset();

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
    if (state_() != S_CLOSED)
    {
        gcs_.close();
    }

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::async_recv(void* recv_ctx)
{
    assert(recv_ctx != 0);

    if (state_() == S_CLOSED || state_() == S_CLOSING)
    {
        log_error <<"async recv cannot start, provider in closed/closing state";
        return WSREP_FATAL;
    }

    ++receivers_;
    as_ = &gcs_as_;

    wsrep_status_t retval(WSREP_OK);

    while (WSREP_OK == retval && state_() != S_CLOSING)
    {
        ssize_t rc;

        while ((rc = as_->process(recv_ctx)) == -ECANCELED)
        {
            recv_IST(recv_ctx);
            // hack: prevent fast looping until ist controlling thread
            // resumes gcs prosessing
            usleep(10000);
        }

        if (rc <= 0)
        {
            retval = WSREP_CONN_FAIL;
        }
    }

    if (receivers_.sub_and_fetch(1) == 0)
    {
        if (state_() != S_CLOSING)
        {
            log_warn << "Broken shutdown sequence, provider state: "
                     << state_() << ", retval: " << retval;
            assert (0);
            /* avoid abort in production */
            state_.shift_to(S_CLOSING);
        }
        state_.shift_to(S_CLOSED);
    }

    return retval;
}

galera::TrxHandle*
galera::ReplicatorSMM::local_trx(wsrep_trx_id_t trx_id)
{
    return wsdb_.get_trx(trx_proto_ver_, uuid_, trx_id, false);
}

galera::TrxHandle*
galera::ReplicatorSMM::local_trx(wsrep_trx_handle_t* handle, bool create)
{
    TrxHandle* trx;
    assert(handle != 0);

    if (handle->opaque != 0)
    {
        trx = reinterpret_cast<TrxHandle*>(handle->opaque);
        assert(trx->trx_id() == handle->trx_id ||
               wsrep_trx_id_t(-1) == handle->trx_id);
        trx->ref();
    }
    else
    {
        trx = wsdb_.get_trx(trx_proto_ver_, uuid_, handle->trx_id, create);
        handle->opaque = trx;
    }

    return trx;
}


void galera::ReplicatorSMM::unref_local_trx(TrxHandle* trx)
{
    wsdb_.unref_trx(trx);
}


void galera::ReplicatorSMM::discard_local_trx(wsrep_trx_id_t trx_id)
{
    wsdb_.discard_trx(trx_id);
}


galera::TrxHandle*
galera::ReplicatorSMM::local_conn_trx(wsrep_conn_id_t conn_id, bool create)
{
    return wsdb_.get_conn_query(trx_proto_ver_, uuid_, conn_id, create);
}


void galera::ReplicatorSMM::discard_local_conn_trx(wsrep_conn_id_t conn_id)
{
    wsdb_.discard_conn_query(conn_id);
}


void galera::ReplicatorSMM::discard_local_conn(wsrep_conn_id_t conn_id)
{
    wsdb_.discard_conn(conn_id);
}


void galera::ReplicatorSMM::apply_trx(void* recv_ctx, TrxHandle* trx)
{
    assert(trx != 0);
    assert(trx->global_seqno() > 0);
    assert(trx->is_certified() == true);
    assert(trx->global_seqno() > apply_monitor_.last_left());
    assert(trx->is_local() == false);

    ApplyOrder ao(*trx);
    CommitOrder co(*trx, co_mode_);

    gu_trace(apply_monitor_.enter(ao));
    trx->set_state(TrxHandle::S_APPLYING);

    wsrep_trx_meta_t meta = {{state_uuid_, trx->global_seqno() },
                             trx->depends_seqno()};
    gu_trace(apply_trx_ws(recv_ctx, apply_cb_, commit_cb_, *trx, meta));
    // at this point any exception in apply_trx_ws() is fatal, not
    // catching anything.
    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        gu_trace(commit_monitor_.enter(co));
        trx->set_state(TrxHandle::S_COMMITTING);
        if (gu_unlikely (WSREP_OK != commit_cb_(recv_ctx, &meta, true)))
            gu_throw_fatal << "Commit failed. Trx: " << trx;

        commit_monitor_.leave(co);
        trx->set_state(TrxHandle::S_COMMITTED);
    }
    else
    {
        trx->set_state(TrxHandle::S_COMMITTING);
        if (gu_unlikely (WSREP_OK != commit_cb_(recv_ctx, &meta, true)))
            gu_throw_fatal << "Commit failed. Trx: " << trx;
        trx->set_state(TrxHandle::S_COMMITTED);
    }
    apply_monitor_.leave(ao);

    if (trx->local_seqno() != -1)
    {
        // trx with local seqno -1 originates from IST (or other source not gcs)
        report_last_committed(cert_.set_trx_committed(trx));
    }
}

wsrep_status_t galera::ReplicatorSMM::replicate(TrxHandle* trx)
{
    if (state_() < S_JOINED) return WSREP_TRX_FAIL;

    assert(trx->state() == TrxHandle::S_EXECUTING ||
           trx->state() == TrxHandle::S_MUST_ABORT);
    assert(trx->local_seqno() == WSREP_SEQNO_UNDEFINED &&
           trx->global_seqno() == WSREP_SEQNO_UNDEFINED);

    wsrep_status_t retval(WSREP_TRX_FAIL);

    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
    must_abort:
        trx->set_state(TrxHandle::S_ABORTING);
        return retval;
    }

    std::vector<gu::Buf> actv;

    gcs_action act;
    act.type = GCS_ACT_TORDERED;
#ifndef NDEBUG
    act.seqno_g = GCS_SEQNO_ILL;
#endif

    if (trx->new_version())
    {
        act.buf  = NULL;
        act.size = trx->write_set_out().gather(trx->source_id(),
                                               trx->conn_id(),
                                               trx->trx_id(),
                                               actv);
    }
    else
    {
        trx->set_last_seen_seqno(last_committed());
        assert (trx->last_seen_seqno() >= 0);
        trx->flush(0);

        const MappedBuffer& wscoll(trx->write_set_collection());

        act.buf  = &wscoll[0];
        act.size = wscoll.size();

        assert (act.buf != NULL);
        assert (act.size > 0);
    }

    trx->set_state(TrxHandle::S_REPLICATING);

    ssize_t rcode(-1);

    do
    {
        assert(act.seqno_g == GCS_SEQNO_ILL);

        const ssize_t gcs_handle(gcs_.schedule());

        if (gu_unlikely(gcs_handle < 0))
        {
            log_debug << "gcs schedule " << strerror(-gcs_handle);
            trx->set_state(TrxHandle::S_MUST_ABORT);
            goto must_abort;
        }

        trx->set_gcs_handle(gcs_handle);

        if (trx->new_version())
        {
            trx->set_last_seen_seqno(last_committed());
            assert(trx->last_seen_seqno() >= 0);
            trx->unlock();
            assert (act.buf == NULL); // just a sanity check
            rcode = gcs_.replv(actv, act, true);
        }
        else
        {
            assert(trx->last_seen_seqno() >= 0);
            trx->unlock();
            assert (act.buf != NULL);
            rcode = gcs_.repl(act, true);
        }

        trx->lock();
    }
    while (rcode == -EAGAIN && trx->state() != TrxHandle::S_MUST_ABORT &&
           (usleep(1000), true));

    assert(trx->last_seen_seqno() >= 0);

    if (rcode < 0)
    {
        if (rcode != -EINTR)
        {
            log_debug << "gcs_repl() failed with " << strerror(-rcode)
                      << " for trx " << *trx;
        }

        assert(rcode != -EINTR || trx->state() == TrxHandle::S_MUST_ABORT);
        assert(act.seqno_l == GCS_SEQNO_ILL && act.seqno_g == GCS_SEQNO_ILL);
        assert(NULL == act.buf || !trx->new_version());

        if (trx->state() != TrxHandle::S_MUST_ABORT)
        {
            trx->set_state(TrxHandle::S_MUST_ABORT);
        }

        trx->set_gcs_handle(-1);
        goto must_abort;
    }

    assert(act.buf != NULL);
    assert(act.size == rcode);
    assert(act.seqno_l != GCS_SEQNO_ILL);
    assert(act.seqno_g != GCS_SEQNO_ILL);

    ++replicated_;
    replicated_bytes_ += rcode;
    trx->set_gcs_handle(-1);

    if (trx->new_version())
    {
        gu_trace(trx->unserialize(reinterpret_cast<const gu::byte_t*>(act.buf),
                                  act.size, 0));
    }

    trx->set_received(act.buf, act.seqno_l, act.seqno_g);

    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        retval = cert_for_aborted(trx);

        if (retval != WSREP_BF_ABORT)
        {
            LocalOrder  lo(*trx);
            ApplyOrder  ao(*trx);
            CommitOrder co(*trx, co_mode_);
            local_monitor_.self_cancel(lo);
            apply_monitor_.self_cancel(ao);
            if (co_mode_ !=CommitOrder::BYPASS) commit_monitor_.self_cancel(co);
        }

        if (trx->state() == TrxHandle::S_MUST_ABORT) goto must_abort;
    }
    else
    {
        retval = WSREP_OK;
    }

    assert(trx->last_seen_seqno() >= 0);

    return retval;
}

void
galera::ReplicatorSMM::abort_trx(TrxHandle* trx)
{
    assert(trx != 0);
    assert(trx->is_local() == true);

    log_debug << "aborting trx " << *trx << " " << trx;

    ++local_bf_aborts_;

    switch (trx->state())
    {
    case TrxHandle::S_MUST_ABORT:
    case TrxHandle::S_ABORTING: // guess this is here because we can have a race
        return;
    case TrxHandle::S_EXECUTING:
        trx->set_state(TrxHandle::S_MUST_ABORT);
        break;
    case TrxHandle::S_REPLICATING:
    {
        trx->set_state(TrxHandle::S_MUST_ABORT);
        // trx is in gcs repl
        int rc;
        if (trx->gcs_handle() > 0 &&
            ((rc = gcs_.interrupt(trx->gcs_handle()))) != 0)
        {
            log_debug << "gcs_interrupt(): handle "
                      << trx->gcs_handle()
                      << " trx id " << trx->trx_id()
                      << ": " << strerror(-rc);
        }
        break;
    }
    case TrxHandle::S_CERTIFYING:
    {
        trx->set_state(TrxHandle::S_MUST_ABORT);
        // trx is waiting in local monitor
        LocalOrder lo(*trx);
        trx->unlock();
        local_monitor_.interrupt(lo);
        trx->lock();
        break;
    }
    case TrxHandle::S_APPLYING:
    {
        trx->set_state(TrxHandle::S_MUST_ABORT);
        // trx is waiting in apply monitor
        ApplyOrder ao(*trx);
        trx->unlock();
        apply_monitor_.interrupt(ao);
        trx->lock();
        break;
    }
    case TrxHandle::S_COMMITTING:
        trx->set_state(TrxHandle::S_MUST_ABORT);
        if (co_mode_ != CommitOrder::BYPASS)
        {
            // trx waiting in commit monitor
            CommitOrder co(*trx, co_mode_);
            trx->unlock();
            commit_monitor_.interrupt(co);
            trx->lock();
        }
        break;
    default:
        gu_throw_fatal << "invalid state " << trx->state();
    }
}


wsrep_status_t galera::ReplicatorSMM::pre_commit(TrxHandle*        trx,
                                                 wsrep_trx_meta_t* meta)
{
    assert(trx->state() == TrxHandle::S_REPLICATING);
    assert(trx->local_seqno()  > -1);
    assert(trx->global_seqno() > -1);
    assert(trx->last_seen_seqno() >= 0);

    if (meta != 0)
    {
        meta->gtid.uuid  = state_uuid_;
        meta->gtid.seqno = trx->global_seqno();
        meta->depends_on = trx->depends_seqno();
    }
    // State should not be checked here: If trx has been replicated,
    // it has to be certified and potentially applied. #528
    // if (state_() < S_JOINED) return WSREP_TRX_FAIL;

    wsrep_status_t retval(cert(trx));

    if (gu_unlikely(retval != WSREP_OK))
    {
        assert(trx->state() == TrxHandle::S_MUST_ABORT ||
               trx->state() == TrxHandle::S_MUST_REPLAY_AM ||
               trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY);

        if (trx->state() == TrxHandle::S_MUST_ABORT)
        {
            trx->set_state(TrxHandle::S_ABORTING);
        }

        return retval;
    }

    assert(trx->state() == TrxHandle::S_CERTIFYING);
    assert(trx->global_seqno() > apply_monitor_.last_left());
    trx->set_state(TrxHandle::S_APPLYING);

    ApplyOrder ao(*trx);
    CommitOrder co(*trx, co_mode_);
    bool interrupted(false);

    try
    {
        gu_trace(apply_monitor_.enter(ao));
    }
    catch (gu::Exception& e)
    {
        if (e.get_errno() == EINTR) { interrupted = true; }
        else throw;
    }

    if (gu_unlikely(interrupted) || trx->state() == TrxHandle::S_MUST_ABORT)
    {
        assert(trx->state() == TrxHandle::S_MUST_ABORT);
        if (interrupted) trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
        else             trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        retval = WSREP_BF_ABORT;
    }
    else if ((trx->flags() & TrxHandle::F_COMMIT) != 0)
    {
        trx->set_state(TrxHandle::S_COMMITTING);
        if (co_mode_ != CommitOrder::BYPASS)
        {
            try
            {
                gu_trace(commit_monitor_.enter(co));
            }
            catch (gu::Exception& e)
            {
                if (e.get_errno() == EINTR) { interrupted = true; }
                else throw;
            }

            if (gu_unlikely(interrupted) ||
                trx->state() == TrxHandle::S_MUST_ABORT)
            {
                assert(trx->state() == TrxHandle::S_MUST_ABORT);
                if (interrupted) trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
                else             trx->set_state(TrxHandle::S_MUST_REPLAY);
                retval = WSREP_BF_ABORT;
            }
        }
    }
    else
    {
        trx->set_state(TrxHandle::S_EXECUTING);
    }

    assert((retval == WSREP_OK && (trx->state() == TrxHandle::S_COMMITTING ||
                                   trx->state() == TrxHandle::S_EXECUTING))
           ||
           (retval == WSREP_TRX_FAIL && trx->state() == TrxHandle::S_ABORTING)
           ||
           (retval == WSREP_BF_ABORT && (
               trx->state() == TrxHandle::S_MUST_REPLAY_AM ||
               trx->state() == TrxHandle::S_MUST_REPLAY_CM ||
               trx->state() == TrxHandle::S_MUST_REPLAY)));

    return retval;
}

wsrep_status_t galera::ReplicatorSMM::replay_trx(TrxHandle* trx, void* trx_ctx)
{
    assert(trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY ||
           trx->state() == TrxHandle::S_MUST_REPLAY_AM       ||
           trx->state() == TrxHandle::S_MUST_REPLAY_CM       ||
           trx->state() == TrxHandle::S_MUST_REPLAY);
    assert(trx->trx_id() != static_cast<wsrep_trx_id_t>(-1));
    assert(trx->global_seqno() > apply_monitor_.last_left());

    wsrep_status_t retval(WSREP_OK);

    switch (trx->state())
    {
    case TrxHandle::S_MUST_CERT_AND_REPLAY:
        retval = cert(trx);
        if (retval != WSREP_OK)
        {
            // apply monitor is self canceled in cert
            break;
        }
        trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
        // fall through
    case TrxHandle::S_MUST_REPLAY_AM:
    {
        // safety measure to make sure that all preceding trxs finish before
        // replaying
        trx->set_depends_seqno(trx->global_seqno() - 1);
        ApplyOrder ao(*trx);
        gu_trace(apply_monitor_.enter(ao));
        trx->set_state(TrxHandle::S_MUST_REPLAY_CM);
        // fall through
    }
    case TrxHandle::S_MUST_REPLAY_CM:
        if (co_mode_ != CommitOrder::BYPASS)
        {
            CommitOrder co(*trx, co_mode_);
            gu_trace(commit_monitor_.enter(co));
        }
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        // fall through
    case TrxHandle::S_MUST_REPLAY:
        ++local_replays_;
        trx->set_state(TrxHandle::S_REPLAYING);

        try
        {
            wsrep_trx_meta_t meta = {{state_uuid_, trx->global_seqno() },
                                     trx->depends_seqno()};
            gu_trace(apply_trx_ws(trx_ctx, apply_cb_, commit_cb_, *trx, meta));

            if (gu_unlikely(WSREP_OK != commit_cb_(trx_ctx, &meta, true)))
                gu_throw_fatal << "Commit failed. Trx: " << trx;
        }
        catch (gu::Exception& e)
        {
            st_.mark_corrupt();
            throw;
        }

        // apply, commit monitors are released in post commit
        return WSREP_OK;
    default:
        gu_throw_fatal << "Invalid state in replay for trx " << *trx;
    }

    log_debug << "replaying failed for trx " << *trx;
    trx->set_state(TrxHandle::S_ABORTING);

    return retval;
}


wsrep_status_t galera::ReplicatorSMM::post_commit(TrxHandle* trx)
{
    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        // This is possible in case of ALG: BF applier BF aborts
        // trx that has already grabbed commit monitor and is committing.
        // However, this should be acceptable assuming that commit
        // operation does not reserve any more resources and is able
        // to release already reserved resources.
        log_debug << "trx was BF aborted during commit: " << *trx;
        // manipulate state to avoid crash
        trx->set_state(TrxHandle::S_MUST_REPLAY);
        trx->set_state(TrxHandle::S_REPLAYING);
    }
    assert(trx->state() == TrxHandle::S_COMMITTING ||
           trx->state() == TrxHandle::S_REPLAYING);
    assert(trx->local_seqno() > -1 && trx->global_seqno() > -1);

    CommitOrder co(*trx, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.leave(co);

    ApplyOrder ao(*trx);
    apply_monitor_.leave(ao);

    report_last_committed(cert_.set_trx_committed(trx));
    trx->set_state(TrxHandle::S_COMMITTED);

    ++local_commits_;

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::post_rollback(TrxHandle* trx)
{
    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        trx->set_state(TrxHandle::S_ABORTING);
    }

    assert(trx->state() == TrxHandle::S_ABORTING ||
           trx->state() == TrxHandle::S_EXECUTING);

    trx->set_state(TrxHandle::S_ROLLED_BACK);

    // Trx was either rolled back by user or via certification failure,
    // last committed report not needed since cert index state didn't change.
    // report_last_committed();
    ++local_rollbacks_;

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::causal_read(wsrep_gtid_t* gtid)
{
    wsrep_seqno_t cseq(static_cast<wsrep_seqno_t>(gcs_.caused()));

    if (cseq < 0)
    {
        log_warn << "gcs_caused() returned " << cseq << " (" << strerror(-cseq)
                 << ')';
        return WSREP_TRX_FAIL;
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
        gu::datetime::Date wait_until(gu::datetime::Date::calendar()
                                      + causal_read_timeout_);
        if (gu_likely(co_mode_ != CommitOrder::BYPASS))
        {
            commit_monitor_.wait(cseq, wait_until);
        }
        else
        {
            apply_monitor_.wait(cseq, wait_until);
        }
        if (gtid != 0)
        {
            gtid->uuid = state_uuid_;
            gtid->seqno = cseq;
        }
        ++causal_reads_;
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_debug << "monitor wait failed for causal read: " << e.what();
        return WSREP_TRX_FAIL;
    }
}


wsrep_status_t galera::ReplicatorSMM::to_isolation_begin(TrxHandle*        trx,
                                                         wsrep_trx_meta_t* meta)
{
    if (meta != 0)
    {
        meta->gtid.uuid  = state_uuid_;
        meta->gtid.seqno = trx->global_seqno();
        meta->depends_on = trx->depends_seqno();
    }

    assert(trx->state() == TrxHandle::S_REPLICATING);
    assert(trx->trx_id() == static_cast<wsrep_trx_id_t>(-1));
    assert(trx->local_seqno() > -1 && trx->global_seqno() > -1);
    assert(trx->global_seqno() > apply_monitor_.last_left());

    wsrep_status_t retval;
    switch ((retval = cert(trx)))
    {
    case WSREP_OK:
    {
        ApplyOrder ao(*trx);
        CommitOrder co(*trx, co_mode_);

        gu_trace(apply_monitor_.enter(ao));

        if (co_mode_ != CommitOrder::BYPASS)
            try
            {
                commit_monitor_.enter(co);
            }
            catch (...)
            {
                gu_throw_fatal << "unable to enter commit monitor: " << *trx;
            }

        trx->set_state(TrxHandle::S_APPLYING);
        log_debug << "Executing TO isolated action: " << *trx;
        st_.mark_unsafe();
        break;
    }
    case WSREP_TRX_FAIL:
        // Apply monitor is released in cert() in case of failure.
        trx->set_state(TrxHandle::S_ABORTING);
        // Called now from cert()
        // report_last_committed();
        break;
    default:
        log_error << "unrecognized retval "
                  << retval
                  << " for to isolation certification for "
                  << *trx;
        retval = WSREP_FATAL;
        break;
    }

    return retval;
}


wsrep_status_t galera::ReplicatorSMM::to_isolation_end(TrxHandle* trx)
{
    assert(trx->state() == TrxHandle::S_APPLYING);

    log_debug << "Done executing TO isolated action: " << *trx;

    CommitOrder co(*trx, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.leave(co);
    ApplyOrder ao(*trx);
    apply_monitor_.leave(ao);

    st_.mark_safe();
    report_last_committed(cert_.set_trx_committed(trx));

    return WSREP_OK;
}


wsrep_status_t
galera::ReplicatorSMM::sst_sent(const wsrep_uuid_t& uuid, wsrep_seqno_t seqno)
{
    if (state_() != S_DONOR)
    {
        log_error << "sst sent called when not SST donor, state " << state_();
        return WSREP_CONN_FAIL;
    }

    if (uuid != state_uuid_ && seqno >= 0)
    {
        // state we have sent no longer corresponds to the current group state
        // mark an error
        seqno = -EREMCHG;
    }

    try {
        // #557 - remove this if() when we return back to joining after SST
        if (!ist_sst_ || seqno < 0) gcs_.join(seqno);
        ist_sst_ = false;
        return WSREP_OK;
    }
    catch (gu::Exception& e)
    {
        log_error << "failed to recover from DONOR state: " << e.what();
        return WSREP_CONN_FAIL;
    }
}


void galera::ReplicatorSMM::process_trx(void* recv_ctx, TrxHandle* trx)
{
    assert(recv_ctx != 0);
    assert(trx != 0);
    assert(trx->local_seqno() > 0);
    assert(trx->global_seqno() > 0);
    assert(trx->last_seen_seqno() >= 0);
    assert(trx->depends_seqno() == -1);
    assert(trx->state() == TrxHandle::S_REPLICATING);

    wsrep_status_t const retval(cert(trx));

    switch (retval)
    {
    case WSREP_OK:
        try
        {
            gu_trace(apply_trx(recv_ctx, trx));
        }
        catch (std::exception& e)
        {
            st_.mark_corrupt();

            log_fatal << "Failed to apply trx: " << *trx;
            log_fatal << e.what();
            log_fatal << "Node consistency compromized, aborting...";
            abort();
        }
        break;
    case WSREP_TRX_FAIL:
        // certification failed, apply monitor has been canceled
        trx->set_state(TrxHandle::S_ABORTING);
        trx->set_state(TrxHandle::S_ROLLED_BACK);
        break;
    default:
        // this should not happen for remote actions
        gu_throw_error(EINVAL)
            << "unrecognized retval for remote trx certification: "
            << retval << " trx: " << *trx;
    }
}


void galera::ReplicatorSMM::process_commit_cut(wsrep_seqno_t seq,
                                               wsrep_seqno_t seqno_l)
{
    assert(seq > 0);
    assert(seqno_l > 0);
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    cert_.purge_trxs_upto(seq);
    local_monitor_.leave(lo);
    log_debug << "Got commit cut from GCS: " << seq;
}

void galera::ReplicatorSMM::establish_protocol_versions (int proto_ver)
{
    switch (proto_ver)
    {
    case 1:
        trx_proto_ver_ = 1;
        str_proto_ver_ = 0;
        break;
    case 2:
        trx_proto_ver_ = 1;
        str_proto_ver_ = 1;
        break;
    case 3:
    case 4:
        trx_proto_ver_ = 2;
        str_proto_ver_ = 1;
        break;
    case 5:
        trx_proto_ver_ = 3;
        str_proto_ver_ = 1;
        break;
    default:
        log_fatal << "Configuration change resulted in an unsupported protocol "
            "version: " << proto_ver << ". Can't continue.";
        abort();
    };

    protocol_version_ = proto_ver;
    log_info << "REPL Protocols: " << protocol_version_ << " ("
              << trx_proto_ver_ << ", " << str_proto_ver_ << ")";
}

static bool
app_wants_state_transfer (const void* const req, ssize_t const req_len)
{
    return (req_len != (strlen(WSREP_STATE_TRANSFER_NONE) + 1) ||
            memcmp(req, WSREP_STATE_TRANSFER_NONE, req_len));
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

void
galera::ReplicatorSMM::process_conf_change(void*                    recv_ctx,
                                           const wsrep_view_info_t& view_info,
                                           int                      repl_proto,
                                           State                    next_state,
                                           wsrep_seqno_t            seqno_l)
{
    assert(seqno_l > -1);

    update_incoming_list(view_info);

    LocalOrder lo(seqno_l);
    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());

    apply_monitor_.drain(upto);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);

    if (view_info.my_idx >= 0)
    {
        uuid_ = view_info.members[view_info.my_idx].id;
    }

    bool const          st_required(state_transfer_required(view_info));
    wsrep_seqno_t const group_seqno(view_info.seqno);
    const wsrep_uuid_t& group_uuid (view_info.uuid);

    if (st_required)
    {
        log_info << "State transfer required: "
                 << "\n\tGroup state: " << group_uuid << ":" << group_seqno
                 << "\n\tLocal state: " << state_uuid_<< ":"
                 << apply_monitor_.last_left();

        if (S_CONNECTED != state_()) state_.shift_to(S_CONNECTED);
    }

    void*   app_req(0);
    ssize_t app_req_len(0);

    const_cast<wsrep_view_info_t&>(view_info).state_gap = st_required;
    view_cb_(app_ctx_, recv_ctx, &view_info, 0, 0, &app_req, &app_req_len);

    if (app_req_len < 0)
    {
        close();
        gu_throw_fatal << "View callback failed: " << -app_req_len << " ("
                       << strerror(-app_req_len) << "). This is unrecoverable, "
                       << "restart required.";
    }
    else if (st_required && 0 == app_req_len && state_uuid_ != group_uuid)
    {
        close();
        gu_throw_fatal << "Local state UUID " << state_uuid_
                       << " is different from group state UUID " << group_uuid
                       << ", and SST request is null: restart required.";
    }

    if (view_info.view >= 0) // Primary configuration
    {
        establish_protocol_versions (repl_proto);

        // we have to reset cert initial position here, SST does not contain
        // cert index yet (see #197).
        cert_.assign_initial_position(group_seqno, trx_proto_ver_);

        // record state seqno, needed for IST on DONOR
        cc_seqno_ = group_seqno;

        bool const app_wants_st(app_wants_state_transfer(app_req, app_req_len));

        if (st_required && app_wants_st)
        {
            request_state_transfer (recv_ctx,
                                    group_uuid, group_seqno, app_req,
                                    app_req_len);
        }
        else
        {
            if (view_info.view == 1 || !app_wants_st)
            {
                update_state_uuid (group_uuid);
                apply_monitor_.set_initial_position(group_seqno);
                if (co_mode_ != CommitOrder::BYPASS)
                    commit_monitor_.set_initial_position(group_seqno);
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
                    synced_cb_(app_ctx_);
                    break;
                default:
                    log_debug << "next_state " << next_state;
                    break;
                }
            }

            st_.set(state_uuid_, WSREP_SEQNO_UNDEFINED);
        }

        if (state_() == S_JOINING && sst_state_ != SST_NONE)
        {
            /* There are two reasons we can be here:
             * 1) we just got state transfer in request_state_transfer() above;
             * 2) we failed here previously (probably due to partition).
             */
            try {
                gcs_.join(sst_seqno_);
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
        if (state_uuid_ != WSREP_UUID_UNDEFINED)
        {
            st_.set (state_uuid_, apply_monitor_.last_left());
        }

        if (next_state != S_CONNECTED && next_state != S_CLOSING)
        {
            log_fatal << "Internal error: unexpected next state for "
                      << "non-prim: " << next_state << ". Restart required.";
            abort();
        }

        state_.shift_to(next_state);
    }

    local_monitor_.leave(lo);
    gcs_.resume_recv();
    free(app_req);
}


void galera::ReplicatorSMM::process_join(wsrep_seqno_t seqno_j,
                                         wsrep_seqno_t seqno_l)
{
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());

    apply_monitor_.drain(upto);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);

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

    apply_monitor_.drain(upto);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);

    state_.shift_to(S_SYNCED);
    synced_cb_(app_ctx_);
    local_monitor_.leave(lo);
}

wsrep_seqno_t galera::ReplicatorSMM::pause()
{
    try
    {
        gu_trace(local_monitor_.lock());
    }
    catch (gu::Exception& e)
    {
        if (e.get_errno() == EALREADY) return cert_.position();
        throw;
    }

    wsrep_seqno_t const ret(cert_.position());

    apply_monitor_.drain(ret);
    assert (apply_monitor_.last_left() >= ret);

    if (co_mode_ != CommitOrder::BYPASS)
    {
        commit_monitor_.drain(ret);
        assert (commit_monitor_.last_left() >= ret);
    }

    st_.set(state_uuid_, ret);

    log_info << "Provider paused at " << state_uuid_ << ':' << ret;

    return ret;
}

void galera::ReplicatorSMM::resume()
{
    st_.set(state_uuid_, WSREP_SEQNO_UNDEFINED);

    try
    {
        gu_trace(local_monitor_.unlock());
    }
    catch (gu::Exception& e)
    {
        if (e.get_errno() == EBUSY)
        {
            /* monitor is still locked, restore saved state */
            st_.set(state_uuid_, cert_.position());
            return;
        }
        throw;
    }

    log_info << "Provider resumed.";
}

void galera::ReplicatorSMM::desync()
{
    wsrep_seqno_t seqno_l;

    ssize_t const ret(gcs_.desync(&seqno_l));

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
            state_.shift_to(S_DONOR);
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
    gcs_.join(commit_monitor_.last_left());
}


//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
////                           Private
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

wsrep_status_t galera::ReplicatorSMM::cert(TrxHandle* trx)
{
    assert(trx->state() == TrxHandle::S_REPLICATING ||
           trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY);

    assert(trx->local_seqno()     != WSREP_SEQNO_UNDEFINED);
    assert(trx->global_seqno()    != WSREP_SEQNO_UNDEFINED);
    assert(trx->last_seen_seqno() >= 0);
    assert(trx->last_seen_seqno() < trx->global_seqno());

    trx->set_state(TrxHandle::S_CERTIFYING);

    LocalOrder  lo(*trx);
    ApplyOrder  ao(*trx);
    CommitOrder co(*trx, co_mode_);
    bool interrupted(false);

    try
    {
        gu_trace(local_monitor_.enter(lo));
    }
    catch (gu::Exception& e)
    {
        if (e.get_errno() == EINTR) { interrupted = true; }
        else throw;
    }

    wsrep_status_t retval(WSREP_OK);

    if (gu_likely (!interrupted))
    {
        switch (cert_.append_trx(trx))
        {
        case Certification::TEST_OK:
            if (trx->global_seqno() > apply_monitor_.last_left())
            {
                if (trx->state() == TrxHandle::S_CERTIFYING)
                {
                    retval = WSREP_OK;
                }
                else
                {
                    assert(trx->state() == TrxHandle::S_MUST_ABORT);
                    trx->set_state(TrxHandle::S_MUST_REPLAY_AM);
                    retval = WSREP_BF_ABORT;
                }
            }
            else
            {
                // this can happen after SST position has been submitted
                // but not all actions preceding SST initial position
                // have been processed
                trx->set_state(TrxHandle::S_MUST_ABORT);
                report_last_committed(cert_.set_trx_committed(trx));
                retval = WSREP_TRX_FAIL;
            }
            break;
        case Certification::TEST_FAILED:
            if (trx->global_seqno() > apply_monitor_.last_left())
            {
                if (gu_unlikely(trx->is_toi())) // small sanity check
                {
                    log_error << "Certification failed for TO isolated action: "
                              << *trx;
                    assert(0); // should never happen
                }
                apply_monitor_.self_cancel(ao);
                if (co_mode_ != CommitOrder::BYPASS)
                    commit_monitor_.self_cancel(co);
            }
            trx->set_state(TrxHandle::S_MUST_ABORT);
            local_cert_failures_ += trx->is_local();
            report_last_committed(cert_.set_trx_committed(trx));
            retval = WSREP_TRX_FAIL;
            break;
        }

        // we probably want to do it 'in order' for std::map reasons, so keeping
        // it inside the monitor
        /*! @todo: benchmark both variants on SMP */
        gcache_.seqno_assign (trx->action(),
                              trx->global_seqno(),
                              trx->depends_seqno(),
                              trx->is_local() && !trx->new_version());  // frees local actions

        local_monitor_.leave(lo);
    }
    else
    {
        retval = cert_for_aborted(trx);

        if (retval != WSREP_BF_ABORT)
        {
            local_monitor_.self_cancel(lo);
            apply_monitor_.self_cancel(ao);

            if (co_mode_ != CommitOrder::BYPASS)
                commit_monitor_.self_cancel(co);
        }
    }

    return retval;
}


wsrep_status_t galera::ReplicatorSMM::cert_for_aborted(TrxHandle* trx)
{
    wsrep_status_t retval(WSREP_OK);
    switch (cert_.test(trx, false))
    {
    case Certification::TEST_OK:
        trx->set_state(TrxHandle::S_MUST_CERT_AND_REPLAY);
        retval = WSREP_BF_ABORT;
        break;
    case Certification::TEST_FAILED:
        if (trx->state() != TrxHandle::S_MUST_ABORT)
        {
            trx->set_state(TrxHandle::S_MUST_ABORT);
        }
        retval = WSREP_TRX_FAIL;

        gcache_.seqno_assign (trx->action(),
                              trx->global_seqno(),
                              -1,
                              trx->is_local() && !trx->new_version());
        break;
    }
    return retval;
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

    st_.set(uuid, WSREP_SEQNO_UNDEFINED);
}

void
galera::ReplicatorSMM::abort()
{
    gcs_.close();
    gu_abort();
}
