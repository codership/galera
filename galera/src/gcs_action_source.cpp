//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
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
        case GCS_ACT_WRITESET:
        case GCS_ACT_CCHANGE:
            // these are ordered and should be released when no longer needed
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


void galera::GcsActionSource::dispatch(void* const              recv_ctx,
                                       const struct gcs_action& act,
                                       bool&                    exit_loop)
{
    assert(recv_ctx != 0);
    assert(act.buf != 0);
    assert(act.seqno_l > 0);

    switch (act.type)
    {
    case GCS_ACT_WRITESET:
    {
        assert(act.seqno_g > 0);
        assert(act.seqno_l != GCS_SEQNO_ILL);

        TrxHandleSlavePtr tsp(TrxHandleSlave::New(false, trx_pool_),
                              TrxHandleSlaveDeleter());
        gu_trace(tsp->unserialize<true>(act));

        gu_trace(replicator_.process_trx(recv_ctx, tsp));
        exit_loop = tsp->exit_loop(); // this is the end of trx lifespan
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
    case GCS_ACT_CCHANGE:
        gu_trace(replicator_.process_conf_change(recv_ctx, act));
        break;
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

    /* Potentially we want to do corrupt() check inside commit_monitor_ as well
     * but by the time inconsistency is detected an arbitrary number of
     * transactions may be already committed, so no reason to try that hard
     * in a critical section */
    bool const skip(replicator_.corrupt()       &&
                    GCS_ACT_CCHANGE != act.type);

    if (gu_likely(rc > 0 && !skip))
    {
        Release release(act, gcache_);
        ++received_;
        received_bytes_ += rc;
        gu_trace(dispatch(recv_ctx, act, exit_loop));
    }
    else if (rc > 0 && skip)
    {
        replicator_.cancel_seqnos(act.seqno_l, act.seqno_g);
    }
    else
    {
        assert(act.seqno_l < 0);
        assert(act.seqno_g < 0);
    }

    return rc;
}
