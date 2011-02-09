//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "replicator.hpp"
#include "gcs_action_source.hpp"
#include "trx_handle.hpp"
#include "serialization.hpp"

extern "C"
{
#include "galera_info.h"
}

#include <cassert>

// Exception-safe way to release action pointer when it goes out
// of scope
class Release
{
public:
    Release(void* ptr) : ptr_(ptr) { }
    ~Release() { free(ptr_); }
private:
    Release(const Release&);
    void operator=(Release&);
    void* ptr_;
};


static galera::Replicator::State state2repl(const gcs_act_conf_t* conf)
{
    assert(conf != 0);
    switch (conf->my_state)
    {
    case GCS_NODE_STATE_NON_PRIM:
        if (conf->my_idx >= 0) return galera::Replicator::S_JOINING;
        else                   return galera::Replicator::S_CLOSING;
    case GCS_NODE_STATE_PRIM:
    case GCS_NODE_STATE_JOINER:
        return galera::Replicator::S_JOINING;
    case GCS_NODE_STATE_JOINED:
        return galera::Replicator::S_JOINED;
    case GCS_NODE_STATE_SYNCED:
        return galera::Replicator::S_SYNCED;
    case GCS_NODE_STATE_DONOR:
        return galera::Replicator::S_DONOR;
    default:
        gu_throw_fatal << "unhandled gcs state: " << conf->my_state;
        throw;
    }
}


galera::GcsActionTrx::GcsActionTrx(const void* act, size_t act_size,
                                   gcs_seqno_t seqno_l, gcs_seqno_t seqno_g)
    :
    trx_(new TrxHandle())
{
    assert(seqno_l != GCS_SEQNO_ILL);
    assert(seqno_g != GCS_SEQNO_ILL);
    size_t offset(unserialize(reinterpret_cast<const gu::byte_t*>(act),
                              act_size, 0, *trx_));
    trx_->append_write_set(reinterpret_cast<const gu::byte_t*>(act) + offset,
                           act_size - offset);
    trx_->set_seqnos(seqno_l, seqno_g);
    trx_->lock();
}


galera::GcsActionTrx::~GcsActionTrx()
{
    assert(trx_->refcnt() >= 1);
    trx_->unlock();
    trx_->unref();
}


void galera::GcsActionSource::dispatch(void*          recv_ctx,
                                       const void*    act,
                                       size_t         act_size,
                                       gcs_act_type_t act_type,
                                       wsrep_seqno_t  seqno_l,
                                       wsrep_seqno_t  seqno_g)
{
    assert(recv_ctx != 0);
    assert(act != 0);
    assert(seqno_l > 0);

    switch (act_type)
    {
    case GCS_ACT_TORDERED:
    {
        assert(seqno_g > 0);
        GcsActionTrx trx(act, act_size, seqno_l, seqno_g);
        trx.trx()->set_state(TrxHandle::S_REPLICATING);
        trx.trx()->set_state(TrxHandle::S_REPLICATED);
        replicator_.process_trx(recv_ctx, trx.trx());
        break;
    }
    case GCS_ACT_COMMIT_CUT:
    {
        wsrep_seqno_t seq;
        unserialize(reinterpret_cast<const gu::byte_t*>(act), act_size, 0, seq);
        replicator_.process_commit_cut(seq, seqno_l);
        break;
    }
    case GCS_ACT_CONF:
    {
        const gcs_act_conf_t* conf(
            reinterpret_cast<const gcs_act_conf_t*>(act)
            );
        wsrep_view_info_t* view_info(
            galera_view_info_create(conf, conf->my_state == GCS_NODE_STATE_PRIM)
            );

        replicator_.process_view_info(recv_ctx, *view_info,
                                      state2repl(conf), seqno_l);
        free(view_info);
        break;
    }
    case GCS_ACT_STATE_REQ:
        replicator_.process_state_req(recv_ctx, act, act_size, seqno_l);
        break;
    case GCS_ACT_JOIN:
        replicator_.process_join(seqno_l);
        break;
    case GCS_ACT_SYNC:
        replicator_.process_sync(seqno_l);
        break;
    default:
        gu_throw_fatal << "unrecognized action type: " << act_type;
    }
}


ssize_t galera::GcsActionSource::process(void* recv_ctx)
{
    void* act;
    size_t act_size;
    gcs_act_type_t act_type;
    gcs_seqno_t seqno_g, seqno_l;
    ssize_t rc(gcs_.recv(&act, &act_size, &act_type, &seqno_l, &seqno_g));
    if (rc > 0)
    {
        Release release(act);
        ++received_;
        received_bytes_ += rc;
        dispatch(recv_ctx, act, act_size, act_type, seqno_l, seqno_g);
    }
    return rc;
}
