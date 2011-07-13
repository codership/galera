//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//


#include "replicator_smm.hpp"
#include "galera_exception.hpp"
#include "uuid.hpp"
#include "serialization.hpp"

extern "C"
{
#include "galera_info.h"
}

#include <fstream>
#include <sstream>
#include <iostream>


static wsrep_apply_data_t
init_apply_data (const char* stmt) // NOTE: stmt is NOT copied!
{
    wsrep_apply_data_t ret;

    ret.type           = WSREP_APPLY_SQL;
    ret.u.sql.stm      = stmt;
    ret.u.sql.len      = strlen(ret.u.sql.stm) + 1; // + terminating 0
    ret.u.sql.timeval  = static_cast<time_t>(0);
    ret.u.sql.randseed = 0;

    return ret;
}

static wsrep_apply_data_t commit_stmt  (init_apply_data("COMMIT"));
static wsrep_apply_data_t rollback_stmt(init_apply_data("ROLLBACK"));


static void
apply_data (void*               recv_ctx,
            wsrep_bf_apply_cb_t apply_cb,
            wsrep_apply_data_t& data,
            wsrep_seqno_t       seqno_g) throw (galera::ApplyException)
{
    assert(seqno_g > 0);
    assert(apply_cb != 0);

    wsrep_status_t err = apply_cb (recv_ctx, &data, seqno_g);

    if (gu_unlikely(err != WSREP_OK))
    {
        const char* const err_str(galera::wsrep_status_str(err));
        std::ostringstream os;

        switch (data.type)
        {
        case WSREP_APPLY_SQL:
            os << "Failed to apply SQL statement:\n"
               << "Status: " << err_str << '\n'
               << "Seqno:  " << seqno_g << '\n'
               << "SQL:    " << data.u.sql.stm;
            break;
        case WSREP_APPLY_ROW:
            os << "Failed to apply row buffer: " << data.u.row.buffer
               << ", seqno: "<< seqno_g << ", status: " << err_str;
            break;
        case WSREP_APPLY_APP:
            os << "Failed to apply app buffer: " << data.u.app.buffer
               << ", seqno: "<< seqno_g << ", status: " << err_str;
            break;
        default:
            os << "Unrecognized data type: " << data.type;
        }

        galera::ApplyException ae(os.str(), err);

        GU_TRACE(ae);

        throw ae;
    }

    return;
}


static void
apply_ws (void*                   recv_ctx,
          wsrep_bf_apply_cb_t     apply_cb,
          const galera::WriteSet& ws,
          wsrep_seqno_t           seqno_g)
    throw (galera::ApplyException, gu::Exception)
{
    assert(seqno_g > 0);
    assert(apply_cb != 0);

    wsrep_apply_data_t data = {WSREP_APPLY_APP, {}};
    data.u.app.buffer = const_cast<uint8_t*>(&ws.get_data()[0]);
    data.u.app.len    = ws.get_data().size();

    gu_trace(apply_data (recv_ctx, apply_cb, data, seqno_g));

    return;
}


static inline void
apply_wscoll(void*                    recv_ctx,
             wsrep_bf_apply_cb_t      apply_cb,
             const galera::TrxHandle& trx)
    throw (galera::ApplyException, gu::Exception)
{
    const galera::MappedBuffer& wscoll(trx.write_set_collection());
    // skip over trx header
    size_t offset(galera::serial_size(trx));
    galera::WriteSet ws(trx.version());

    while (offset < wscoll.size())
    {
        offset = unserialize(&wscoll[0], wscoll.size(), offset, ws);

        gu_trace(apply_ws (recv_ctx, apply_cb, ws, trx.global_seqno()));
    }

    assert(offset == wscoll.size());

    return;
}


static void
apply_trx_ws(void*                    recv_ctx,
             wsrep_bf_apply_cb_t      apply_cb,
             const galera::TrxHandle& trx)
    throw (galera::ApplyException, gu::Exception)
{
    static const size_t max_apply_attempts(10);
    size_t attempts(1);

    do
    {
        try
        {
            gu_trace(apply_wscoll(recv_ctx, apply_cb, trx));
            break;
        }
        catch (galera::ApplyException& e)
        {
            if (trx.flags() & galera::TrxHandle::F_ISOLATION)
            {
                log_debug << "ignoring applying error for trx in isolation: "
                          << trx;
                break;
            }
            else
            {
                wsrep_status_t err = e.wsrep_status();

                if (WSREP_TRX_FAIL == err)
                {
                    gu_trace(apply_data(recv_ctx, apply_cb, rollback_stmt,
                                        trx.global_seqno()));
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
    case ReplicatorSMM::S_CLOSED:    return (os << "CLOSED");
    case ReplicatorSMM::S_CLOSING:   return (os << "CLOSING");
    case ReplicatorSMM::S_CONNECTED: return (os << "CONNECTED");
    case ReplicatorSMM::S_JOINING:   return (os << "JOINING");
    case ReplicatorSMM::S_JOINED:    return (os << "JOINED");
    case ReplicatorSMM::S_SYNCED:    return (os << "SYNCED");
    case ReplicatorSMM::S_DONOR:     return (os << "DONOR");
    }

    gu_throw_fatal << "invalid state " << static_cast<int>(state);
    throw;
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
    set_defaults_       (config_, defaults),
    protocol_version_   (args->proto_ver),
    state_              (S_CLOSED),
    sst_state_          (SST_NONE),
    co_mode_            (CommitOrder::from_string(
                             config_.get(Param::commit_order))),
    data_dir_           (),
    state_file_         ("grastate.dat"),
    uuid_               (WSREP_UUID_UNDEFINED),
    state_uuid_         (WSREP_UUID_UNDEFINED),
    state_uuid_str_     (),
    app_ctx_            (args->app_ctx),
    view_cb_            (args->view_handler_cb),
    bf_apply_cb_        (args->bf_apply_cb),
    sst_donate_cb_      (args->sst_donate_cb),
    synced_cb_          (args->synced_cb),
    sst_donor_          (),
    sst_uuid_           (WSREP_UUID_UNDEFINED),
    sst_seqno_          (WSREP_SEQNO_UNDEFINED),
    sst_mutex_          (),
    sst_cond_           (),
    sst_retry_sec_      (1),
    gcs_                (config_, protocol_version_, args->proto_ver,
                         args->node_name, args->node_incoming),
    service_thd_        (gcs_),
    as_                 (0),
    gcs_as_             (gcs_, *this),
    wsdb_               (),
    cert_               (config_),
    local_monitor_      (),
    apply_monitor_      (),
    commit_monitor_     (),
    receivers_          (),
    replicated_         (),
    replicated_bytes_   (),
    local_commits_      (),
    local_rollbacks_    (),
    local_cert_failures_(),
    local_bf_aborts_    (),
    local_replays_      (),
    report_interval_    (128),
    report_counter_     (),
    wsrep_stats_        ()
{
    switch (protocol_version_)
    {
    case 0:
    case 1:
        break;
    default:
        gu_throw_fatal << "unsupported protocol version: "
                       << protocol_version_;
        throw;
    }

    strncpy (const_cast<char*>(state_uuid_str_), 
             "00000000-0000-0000-0000-000000000000", sizeof(state_uuid_str_));

    // @todo add guards (and perhaps actions)
    state_.add_transition(Transition(S_CLOSED,  S_CONNECTED));
    state_.add_transition(Transition(S_CLOSING, S_CLOSED));

    state_.add_transition(Transition(S_CONNECTED, S_CLOSING));
    state_.add_transition(Transition(S_CONNECTED, S_CONNECTED));
    state_.add_transition(Transition(S_CONNECTED, S_JOINING));
    // the following is possible only when bootstrapping new cluster
    // (trivial wsrep_cluster_address)
    state_.add_transition(Transition(S_CONNECTED, S_JOINED));
    // the following is possible on PC remerge
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
        break;
    }
}


wsrep_status_t galera::ReplicatorSMM::connect(const std::string& cluster_name,
                                              const std::string& cluster_url,
                                              const std::string& state_donor)
{
    restore_state(state_file_);
    sst_donor_ = state_donor;
    service_thd_.reset();

    ssize_t err;
    wsrep_status_t ret(WSREP_OK);

    log_info << "Setting initial position to " << state_uuid_ << ':'
             << cert_.position();

    if ((err = gcs_.set_initial_position(state_uuid_, cert_.position())) != 0)
    {
        log_error << "gcs init failed:" << strerror(-err);
        ret = WSREP_NODE_FAIL;
    }

    if (ret == WSREP_OK &&
        (err = gcs_.connect(cluster_name, cluster_url)) != 0)
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

    while (state_() != S_CLOSING)
    {
        ssize_t rc(as_->process(recv_ctx));

        if (rc <= 0)
        {
            retval = WSREP_CONN_FAIL;
//            break;
        }
    }

    if (receivers_.sub_and_fetch(1) == 0)
    {
        state_.shift_to(S_CLOSED);
    }

    return retval;
}

galera::TrxHandle*
galera::ReplicatorSMM::local_trx(wsrep_trx_id_t trx_id)
{
    return wsdb_.get_trx(protocol_version_, uuid_, trx_id, false);
}

galera::TrxHandle*
galera::ReplicatorSMM::local_trx(wsrep_trx_handle_t* handle, bool create)
{
    TrxHandle* trx;
    assert(handle != 0);

    if (handle->opaque != 0)
    {
        trx = reinterpret_cast<TrxHandle*>(handle->opaque);
        assert(trx->trx_id() == handle->trx_id);
        trx->ref();
    }
    else
    {
        trx = wsdb_.get_trx(protocol_version_, uuid_, handle->trx_id, create);
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
    return wsdb_.get_conn_query(protocol_version_, uuid_, conn_id, create);
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
    throw (ApplyException)
{
    assert(trx != 0);
    assert(trx->global_seqno() > 0);
    assert(trx->is_certified() == true);
    assert(trx->global_seqno() > apply_monitor_.last_left());
    assert(trx->is_local() == false);

    ApplyOrder ao(*trx);
    CommitOrder co(*trx, co_mode_);

    gu_trace(apply_monitor_.enter(ao));
    gu_trace(apply_trx_ws(recv_ctx, bf_apply_cb_, *trx));
    // at this point any exception in apply_trx_ws() is fatal, not
    // catching anything.
    if (gu_likely(co_mode_ != CommitOrder::BYPASS))
    {
        gu_trace(commit_monitor_.enter(co));
        gu_trace(apply_data(recv_ctx, bf_apply_cb_, commit_stmt,
                            trx->global_seqno()));
        commit_monitor_.leave(co);
    }
    else
    {
        gu_trace(apply_data(recv_ctx, bf_apply_cb_, commit_stmt,
                            trx->global_seqno()));
    }
    apply_monitor_.leave(ao);

    cert_.set_trx_committed(trx);
    report_last_committed();
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

    trx->set_last_seen_seqno(apply_monitor_.last_left());
    trx->flush(0);
    trx->set_state(TrxHandle::S_REPLICATING);

    gcs_seqno_t seqno_l(GCS_SEQNO_ILL), seqno_g(GCS_SEQNO_ILL);
    const MappedBuffer& wscoll(trx->write_set_collection());

    ssize_t rcode;

    do
    {
        assert(seqno_g == GCS_SEQNO_ILL);

        const ssize_t gcs_handle(gcs_.schedule());

        if (gcs_handle < 0)
        {
            log_debug << "gcs schedule " << strerror(-gcs_handle);
            trx->set_state(TrxHandle::S_MUST_ABORT);
            goto must_abort;
        }

        trx->set_gcs_handle(gcs_handle);
        trx->unlock();

        rcode = gcs_.repl(&wscoll[0], wscoll.size(),
                          GCS_ACT_TORDERED, true, &seqno_l, &seqno_g);
        trx->lock();
    }
    while (rcode == -EAGAIN && trx->state() != TrxHandle::S_MUST_ABORT &&
           (usleep(1000), true));

    if (rcode < 0)
    {
        if (rcode != -EINTR)
        {
            log_debug << "gcs_repl() failed with " << strerror(-rcode)
                      << " for trx " << *trx;
        }

        assert(rcode != -EINTR || trx->state() == TrxHandle::S_MUST_ABORT);
        assert(seqno_l == GCS_SEQNO_ILL && seqno_g == GCS_SEQNO_ILL);

        if (trx->state() != TrxHandle::S_MUST_ABORT)
        {
            trx->set_state(TrxHandle::S_MUST_ABORT);
        }

        trx->set_gcs_handle(-1);
        goto must_abort;
    }

    assert(seqno_l != GCS_SEQNO_ILL && seqno_g != GCS_SEQNO_ILL);
    ++replicated_;
    replicated_bytes_ += wscoll.size();
    trx->set_gcs_handle(-1);
    trx->set_seqnos(seqno_l, seqno_g);

    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        retval = cert_for_aborted(trx);

        if (retval != WSREP_BF_ABORT)
        {
            LocalOrder lo(*trx);
            ApplyOrder ao(*trx);
            CommitOrder co(*trx, co_mode_);
            local_monitor_.self_cancel(lo);
            apply_monitor_.self_cancel(ao);
            if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.self_cancel(co);
        }

        if (trx->state() == TrxHandle::S_MUST_ABORT)
        {
            trx->set_seqnos(WSREP_SEQNO_UNDEFINED, WSREP_SEQNO_UNDEFINED);
            goto must_abort;
        }
    }
    else
    {
        retval = WSREP_OK;
    }

    return retval;
}

void
galera::ReplicatorSMM::abort_trx(TrxHandle* trx) throw (gu::Exception)
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
        throw;
    }
}


wsrep_status_t galera::ReplicatorSMM::pre_commit(TrxHandle* trx)
{
    if (state_() < S_JOINED) return WSREP_TRX_FAIL;

    assert(trx->state() == TrxHandle::S_REPLICATING);
    assert(trx->local_seqno() > -1 && trx->global_seqno() > -1);

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
        trx->set_last_depends_seqno(trx->global_seqno() - 1);
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
        gu_trace(apply_trx_ws(trx_ctx, bf_apply_cb_, *trx));
        gu_trace(apply_data(trx_ctx, bf_apply_cb_, commit_stmt,
                            trx->global_seqno()));

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
    cert_.set_trx_committed(trx);
    trx->set_state(TrxHandle::S_COMMITTED);
    report_last_committed();
    ++local_commits_;
    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::post_rollback(TrxHandle* trx)
{
    if (trx->state() == TrxHandle::S_MUST_ABORT)
    {
        /* @todo: figure out how this can happen? */
//        trx->set_state(TrxHandle::S_ABORTING);
    }

    assert(trx->state() == TrxHandle::S_ABORTING ||
           trx->state() == TrxHandle::S_EXECUTING);

    trx->set_state(TrxHandle::S_ROLLED_BACK);
    report_last_committed();
    ++local_rollbacks_;

    return WSREP_OK;
}


wsrep_status_t galera::ReplicatorSMM::causal_read(wsrep_seqno_t* seqno) const
{
    return WSREP_NOT_IMPLEMENTED;
}



wsrep_status_t galera::ReplicatorSMM::to_isolation_begin(TrxHandle* trx)
{
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
        break;
    }
    case WSREP_TRX_FAIL:
        // Apply monitor is released in cert() in case of failure.
        trx->set_state(TrxHandle::S_ABORTING);
        report_last_committed();
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

    CommitOrder co(*trx, co_mode_);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.leave(co);
    ApplyOrder ao(*trx);
    apply_monitor_.leave(ao);

    cert_.set_trx_committed(trx);
    report_last_committed();

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

    // WARNING: Here we have application block on this call which
    //          may prevent application from resolving the issue.
    //          (Not that we expect that application can resolve it.)
    ssize_t err;
    while (-EAGAIN == (err = gcs_.join(seqno))) usleep (100000);

    if (err == 0) return WSREP_OK;

    log_error << "failed to recover from DONOR state";

    return WSREP_CONN_FAIL;
}


wsrep_status_t
galera::ReplicatorSMM::sst_received(const wsrep_uuid_t& uuid,
                                    wsrep_seqno_t       seqno,
                                    const void*         state,
                                    size_t              state_len)
{
    log_info << "Received SST: " << uuid << ':' << seqno;

    if (state_() != S_JOINING)
    {
        log_error << "not JOINING when sst_received() called, state: "
                  << state_();
        return WSREP_CONN_FAIL;
    }

    gu::Lock lock(sst_mutex_);

    sst_uuid_  = uuid;
    sst_seqno_ = seqno;
    sst_cond_.signal();

    return WSREP_OK;
}


void galera::ReplicatorSMM::process_trx(void* recv_ctx, TrxHandle* trx)
    throw (ApplyException)
{
    assert(recv_ctx != 0);
    assert(trx != 0);
    assert(trx->local_seqno() > 0);
    assert(trx->global_seqno() > 0);
    assert(trx->last_seen_seqno() >= 0);
    assert(trx->last_depends_seqno() == -1);
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
            log_fatal << "Failed to apply trx: " << *trx;
            log_fatal << e.what();
            log_fatal << "Node consistency compromized, aborting...";
            abort();
//            throw;
        }
        break;
    case WSREP_TRX_FAIL:
        // certification failed, apply monitor has been canceled
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
    throw (gu::Exception)
{
    assert(seq > 0);
    assert(seqno_l > 0);
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    cert_.purge_trxs_upto(seq);
    local_monitor_.leave(lo);
}


void
galera::ReplicatorSMM::process_view_info(void*                    recv_ctx,
                                         const wsrep_view_info_t& view_info,
                                         State                    next_state,
                                         wsrep_seqno_t            seqno_l)
    throw (gu::Exception)
{
    assert(seqno_l > -1);
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());

    apply_monitor_.drain(upto);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);

    wsrep_seqno_t const group_seqno(view_info.first - 1);
    const wsrep_uuid_t& group_uuid(view_info.id);

    if (view_info.my_idx >= 0)
    {
        uuid_ = view_info.members[view_info.my_idx].id;
    }

    bool st_req(view_info.state_gap);

    if (st_req)
    {
        assert(view_info.conf >= 0);
        if (state_uuid_ == group_uuid)
        {
            // common history
            if (state_() >= S_JOINING) /* See #442 - S_JOINING should be
                                          a valid state here */
            {
                st_req = (apply_monitor_.last_left() < group_seqno);
            }
            else
            {
                st_req = (apply_monitor_.last_left() != group_seqno);
            }
        }
    }

    if (st_req && S_CONNECTED != state_()) state_.shift_to(S_CONNECTED);

    void* app_req(0);
    ssize_t app_req_len(0);
    // copy that will be eaten by callback
    wsrep_view_info_t *cb_view_info(galera_view_info_copy(&view_info));

    cb_view_info->state_gap = st_req;
    view_cb_(app_ctx_, recv_ctx, cb_view_info, 0, 0, &app_req, &app_req_len);

    if (app_req_len < 0)
    {
        log_fatal << "View callback failed: " << -app_req_len << " ("
                  << strerror(-app_req_len) << "). This is unrecoverable, "
                  << "restart required.";
        abort();
    }

    if (view_info.conf >= 0)
    {
        // Primary configuration
        // we have to reset cert initial position here, SST does not contain
        // cert index yet (see #197).
        protocol_version_ = view_info.proto_ver;
        cert_.assign_initial_position(group_seqno, protocol_version_);

        if (st_req == true)
        {
            request_sst(group_uuid, group_seqno, app_req, app_req_len);
        }
        else
        {
            if (view_info.conf == 1)
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

            invalidate_state(state_file_);
        }

        if (state_() == S_JOINING && sst_state_ != SST_NONE)
        {
            /* There are two reasons we can be here:
             * 1) we just got state transfer in request_sst() above;
             * 2) we failed here previously (probably due to partition).
             */
            ssize_t ret;

            while (-EAGAIN == (ret = gcs_.join(sst_seqno_)))
            {
                log_warn << "Retrying sending JOIN message (seqno: "
                         << sst_seqno_ << ')';
                usleep (100000); // 0.1s
            }

            if (ret >= 0) sst_state_ = SST_NONE;
        }
    }
    else
    {
        // Non-primary configuration
        if (state_uuid_ != WSREP_UUID_UNDEFINED)
        {
            store_state(state_file_);
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
}


void galera::ReplicatorSMM::process_state_req(void* recv_ctx,
                                              const void* req,
                                              size_t req_size,
                                              wsrep_seqno_t const seqno_l,
                                              wsrep_seqno_t const donor_seq)
    throw (gu::Exception)
{
    assert(recv_ctx != 0);
    assert(seqno_l > -1);
    assert(req != 0);

    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));
    apply_monitor_.drain(donor_seq);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(donor_seq);

    state_.shift_to(S_DONOR);
    sst_donate_cb_(app_ctx_, recv_ctx, req, req_size, &state_uuid_,
                   donor_seq, 0, 0);
    local_monitor_.leave(lo);
}


void galera::ReplicatorSMM::process_join(wsrep_seqno_t seqno_l)
    throw (gu::Exception)
{
    LocalOrder lo(seqno_l);

    gu_trace(local_monitor_.enter(lo));

    wsrep_seqno_t const upto(cert_.position());

    apply_monitor_.drain(upto);

    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.drain(upto);

    state_.shift_to(S_JOINED);
    local_monitor_.leave(lo);
}


void galera::ReplicatorSMM::process_sync(wsrep_seqno_t seqno_l)
    throw (gu::Exception)
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

wsrep_seqno_t galera::ReplicatorSMM::pause() throw (gu::Exception)
{
    gu_trace(local_monitor_.lock());

    wsrep_seqno_t const ret(cert_.position());

    apply_monitor_.drain(ret);
    assert (apply_monitor_.last_left() == ret);

    if (co_mode_ != CommitOrder::BYPASS)
    {
        commit_monitor_.drain(ret);
        assert (commit_monitor_.last_left() == ret);
    }

    log_info << "Provider paused at " << state_uuid_ << ':' << ret;

    return ret;
}

void galera::ReplicatorSMM::resume() throw ()
{
    local_monitor_.unlock();
    log_info << "Provider resumed.";
}

void galera::ReplicatorSMM::store_state(const std::string& file) const
{
    std::ofstream fs(file.c_str(), std::ios::trunc);

    if (fs.fail() == true)
    {
        gu_throw_fatal << "could not store state";
    }

    fs << "# GALERA saved state, version: " << 0.8 << ", date: (todo)\n";
    fs << "uuid:  " << state_uuid_ << "\n";
    fs << "seqno: " << apply_monitor_.last_left() << "\n";
    fs << "cert_index:\n";
}

void galera::ReplicatorSMM::restore_state(const std::string& file)
{
    wsrep_uuid_t  uuid  (WSREP_UUID_UNDEFINED);
    wsrep_seqno_t seqno (WSREP_SEQNO_UNDEFINED);
    std::ifstream fs    (file.c_str());

    if (fs.fail() == true)
    {
        log_warn << "state file not found: " << file;
    }
    else
    {
        std::string line;

        getline(fs, line);

        if (fs.good() == false)
        {
            log_warn << "could not read header from state file: " << file;
        }
        else
        {
            log_debug << "read state header: "<< line;

            while (fs.good() == true)
            {
                getline(fs, line);

                if (fs.good() == false) break;

                std::istringstream istr(line);
                std::string        param;

                istr >> param;

                if (param == "uuid:")
                {
                    try
                    {
                        istr >> uuid;
                        log_debug << "read state uuid " << uuid;
                    }
                    catch (gu::Exception& e)
                    {
                        log_error << e.what();
                        uuid = WSREP_UUID_UNDEFINED;
                    }
                }
                else if (param == "seqno:")
                {
                    istr >> seqno;
                    log_debug << "read seqno " << seqno;
                }
                else if (param == "cert_index:")
                {
                    // @todo
                    log_debug << "cert index restore not implemented yet";
                }
            }
        }

        log_info << "Found saved state: " << uuid << ':' << seqno;
    }

    if (seqno < 0 && uuid != WSREP_UUID_UNDEFINED)
    {
        log_warn << "Negative seqno with valid UUID: "
                 << uuid << ':' << seqno << ". Discarding UUID.";
        uuid = WSREP_UUID_UNDEFINED;
    }

    update_state_uuid (uuid);
    apply_monitor_.set_initial_position(seqno);
    if (co_mode_ != CommitOrder::BYPASS) commit_monitor_.set_initial_position(seqno);
    cert_.assign_initial_position(seqno, protocol_version_);
}


void galera::ReplicatorSMM::invalidate_state(const std::string& file) const
{
    std::ofstream fs(file.c_str(), std::ios::trunc);
    if (fs.fail() == true)
    {
        gu_throw_fatal << "could not store state";
    }

    fs << "# GALERA saved state, version: " << 0.8 << ", date: (todo)\n";
    fs << "uuid:  " << WSREP_UUID_UNDEFINED << "\n";
    fs << "seqno: " << WSREP_SEQNO_UNDEFINED << "\n";
    fs << "cert_index:\n";
}

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
////                           Private
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

static bool
retry_sst_request(int ret)
{
    return (ret == -EAGAIN || ret == -ENOTCONN);
}

void
galera::ReplicatorSMM::request_sst(wsrep_uuid_t  const& group_uuid,
                                   wsrep_seqno_t const  group_seqno,
                                   const void* req, size_t req_len)
    throw (gu::Exception)
{
    assert(req != 0);
    log_info << "State transfer required: "
             << "\n\tGroup state: "
             << group_uuid << ":" << group_seqno
             << "\n\tLocal state: " << state_uuid_
             << ":" << apply_monitor_.last_left();

    long ret;
    long tries = 0;
    gu::Lock lock(sst_mutex_);

    do
    {
        invalidate_state(state_file_);

        tries++;

        gcs_seqno_t seqno_l;

        ret = gcs_.request_state_transfer(req, req_len, sst_donor_, &seqno_l);

        if (ret < 0)
        {
            if (!retry_sst_request(ret))
            {
                store_state(state_file_);
                log_error << "Requesting state transfer failed: "
                          << ret << "(" << strerror(-ret) << ")";
            }
            else if (1 == tries)
            {
                log_info << "Requesting state transfer failed: "
                         << ret << "(" << strerror(-ret) << "). "
                         << "Will keep retrying every " << sst_retry_sec_
                         << " second(s)";
            }
        }

        if (seqno_l != GCS_SEQNO_ILL)
        {
            /* Check that we're not running out of space in monitor. */
            if (local_monitor_.would_block(seqno_l))
            {
                long const seconds = sst_retry_sec_ * local_monitor_.size();
                double const hours = (seconds/360) * 0.1;
                log_error << "We ran out of resources, seemingly because "
                          << "we've been unsuccessfully requesting state "
                          << "transfer for over " << seconds << " seconds (>"
                          << hours << " hours). Please check that there is at "
                          << "least one fully synced member in the group. "
                          << "Application must be restarted.";
                ret = -EDEADLK;
            }
            else
            {
                // we are already holding local monitor
                LocalOrder lo(seqno_l);
                local_monitor_.self_cancel(lo);
            }
        }
    }
    while (retry_sst_request(ret) && (usleep(sst_retry_sec_ * 1000000), true));


    if (ret >= 0)
    {
        if (1 == tries)
        {
            log_info << "Requesting state transfer: success, donor: " << ret;
        }
        else
        {
            log_info << "Requesting state transfer: success after "
                     << tries << " tries, donor: " << ret;
        }

        state_.shift_to(S_JOINING);
        sst_state_ = SST_WAIT;

        lock.wait(sst_cond_);

        if (sst_uuid_ != group_uuid || sst_seqno_ < group_seqno)
        {
            log_fatal << "Application received wrong state: "
                      << "\n\tReceived: "
                      << sst_uuid_ <<   ":    " << sst_seqno_
                      << "\n\tRequired: "
                      << group_uuid << ": >= " << group_seqno;
            sst_state_ = SST_FAILED;
            log_fatal << "Application state transfer failed. This is "
                      << "unrecoverable condition, restart required.";
            abort();
        }
        else
        {
            update_state_uuid (sst_uuid_);
            apply_monitor_.set_initial_position(-1);
            apply_monitor_.set_initial_position(sst_seqno_);

            if (co_mode_ != CommitOrder::BYPASS)
            {
                commit_monitor_.set_initial_position(-1);
                commit_monitor_.set_initial_position(sst_seqno_);
            }

            log_debug << "Initial state: " << state_uuid_ << ":" << sst_seqno_;
        }
    }
    else
    {
        sst_state_ = SST_REQ_FAILED;

        if (state_() > S_CLOSING)
        {
            log_fatal << "State transfer request failed unrecoverably: "
                      << -ret << " (" << strerror(-ret) << "). Most likely "
                      << "it is due to inability to communicate with cluster "
                      << "primary component. Restart required.";
            abort();
        }
        else
        {
            // connection is being closed, send failure is expected
        }
    }
}


wsrep_status_t galera::ReplicatorSMM::cert(TrxHandle* trx)
{
    assert(trx->state() == TrxHandle::S_REPLICATING ||
           trx->state() == TrxHandle::S_MUST_CERT_AND_REPLAY);

    assert(trx->local_seqno()     != WSREP_SEQNO_UNDEFINED &&
           trx->global_seqno()    != WSREP_SEQNO_UNDEFINED &&
           trx->last_seen_seqno() != WSREP_SEQNO_UNDEFINED);

    trx->set_state(TrxHandle::S_CERTIFYING);

    LocalOrder lo(*trx);
    ApplyOrder ao(*trx);
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
                if (trx->is_local() == true) ++local_cert_failures_;
                cert_.set_trx_committed(trx);
                retval = WSREP_TRX_FAIL;
            }
            break;
        case Certification::TEST_FAILED:
            if (trx->global_seqno() > apply_monitor_.last_left())
            {
                apply_monitor_.self_cancel(ao);
                if (co_mode_ != CommitOrder::BYPASS)
                    commit_monitor_.self_cancel(co);
            }
            trx->set_state(TrxHandle::S_MUST_ABORT);
            if (trx->is_local() == true) ++local_cert_failures_;
            cert_.set_trx_committed(trx);
            retval = WSREP_TRX_FAIL;
            break;
        }

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
}

void
galera::ReplicatorSMM::abort() throw() /* aborts the program in a clean way */
{
    gcs_.close();
    gu_abort();
}
