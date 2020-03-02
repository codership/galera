//
// Copyright (C) 2010-2020 Codership Oy <info@codership.com>
//

#include "replicator.hpp"
#include "gcs_action_source.hpp"
#include "trx_handle.hpp"

#include "gu_serialize.hpp"

#include "galera_info.hpp"

#include <cassert>

// Exception-safe way to release action pointer when it goes out
// of scope
class Release
{
public:
    Release(struct gcs_action& act, gcache::GCache& gcache)
        :
        act_(act),
        gcache_(gcache)
    {}

    ~Release()
    {
        switch (act_.type)
        {
        case GCS_ACT_TORDERED:
            break;
        case GCS_ACT_STATE_REQ:
            gcache_.free(const_cast<void*>(act_.buf));
            break;
        default:
            ::free(const_cast<void*>(act_.buf));
            break;
        }
    }

private:
    struct gcs_action& act_;
    gcache::GCache&    gcache_;
};


static galera::Replicator::State state2repl(const gcs_act_conf_t& conf)
{
    switch (conf.my_state)
    {
    case GCS_NODE_STATE_NON_PRIM:
        if (conf.my_idx >= 0) return galera::Replicator::S_CONNECTED;
        else                  return galera::Replicator::S_CLOSING;
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
    case GCS_NODE_STATE_MAX:;
    }

    gu_throw_fatal << "unhandled gcs state: " << conf.my_state;
    GU_DEBUG_NORETURN;
}


galera::GcsActionTrx::GcsActionTrx(TrxHandle::SlavePool&    pool,
                                   const struct gcs_action& act)
    :
    trx_(TrxHandle::New(pool))
    // TODO: this dynamic allocation should be unnecessary
{
    assert(act.seqno_l != GCS_SEQNO_ILL);
    assert(act.seqno_g != GCS_SEQNO_ILL);

    const gu::byte_t* const buf = static_cast<const gu::byte_t*>(act.buf);

//    size_t offset(trx_->unserialize(buf, act.size, 0));
    gu_trace(trx_->unserialize(buf, act.size, 0));

    //trx_->append_write_set(buf + offset, act.size - offset);
    // moved to unserialize trx_->set_write_set_buffer(buf + offset, act.size - offset);
    trx_->set_received(act.buf, act.seqno_l, act.seqno_g);
    trx_->lock();
}


galera::GcsActionTrx::~GcsActionTrx()
{
    assert(trx_->refcnt() >= 1);
    trx_->unlock();
    trx_->unref();
}


void galera::GcsActionSource::dispatch(void* const              recv_ctx,
                                       const struct gcs_action& act,
                                       bool&                    exit_loop)
{
    assert(act.buf != 0);
    assert(act.seqno_l > 0);

    switch (act.type)
    {
    case GCS_ACT_TORDERED:
    {
        assert(act.seqno_g > 0);
        GcsActionTrx trx(trx_pool_, act);
        trx.trx()->set_state(TrxHandle::S_REPLICATING);
        gu_trace(replicator_.process_trx(recv_ctx, trx.trx()));
        exit_loop = trx.trx()->exit_loop(); // this is the end of trx lifespan
        break;
    }
    case GCS_ACT_COMMIT_CUT:
    {
        wsrep_seqno_t seq;
        gu::unserialize8(static_cast<const gu::byte_t*>(act.buf), act.size, 0,
                         seq);
        gu_trace(replicator_.process_commit_cut(seq, act.seqno_l));
        break;
    }
    case GCS_ACT_CONF:
    {
        const gcs_act_conf_t* conf(static_cast<const gcs_act_conf_t*>(act.buf));

        wsrep_view_info_t* view_info(
            galera_view_info_create(conf, conf->my_state == GCS_NODE_STATE_PRIM)
            );

        gu_trace(replicator_.process_conf_change(recv_ctx, *view_info,
                                                 conf->repl_proto_ver,
                                                 state2repl(*conf),
                                                 act.seqno_l));
        free(view_info);

        if (conf->conf_id < 0 && conf->memb_num == 0) {
            log_debug << "Received SELF-LEAVE. Closing connection.";
            // called after being shifted to S_CLOSING state.
            gcs_.close();
        }

        break;
    }
    case GCS_ACT_STATE_REQ:
        gu_trace(replicator_.process_state_req(recv_ctx, act.buf, act.size,
                                               act.seqno_l, act.seqno_g));
        break;
    case GCS_ACT_JOIN:
    {
        wsrep_seqno_t seq;
        gu::unserialize8(static_cast<const gu::byte_t*>(act.buf),
                         act.size, 0, seq);
        gu_trace(replicator_.process_join(seq, act.seqno_l));
        break;
    }
    case GCS_ACT_SYNC:
        gu_trace(replicator_.process_sync(act.seqno_l));
        break;
    default:
        gu_throw_fatal << "unrecognized action type: " << act.type;
    }
}


ssize_t galera::GcsActionSource::process(void* recv_ctx, bool& exit_loop)
{
    struct gcs_action act;

    ssize_t rc(gcs_.recv(act));
    if (rc > 0)
    {
        Release release(act, gcache_);
        ++received_;
        received_bytes_ += rc;
        gu_trace(dispatch(recv_ctx, act, exit_loop));
    }
    else if (GCS_ACT_INCONSISTENCY == act.type)
    {
        assert(0 == rc);
        rc = INCONSISTENCY_CODE;
    }
    return rc;
}
