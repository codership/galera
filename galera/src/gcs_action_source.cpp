//
// Copyright (C) 2010-2018 Codership Oy <info@codership.com>
//

#include "replicator.hpp"
#include "gcs_action_source.hpp"
#include "trx_handle.hpp"

#include "gu_serialize.hpp"
#include "gu_throw.hpp"

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

void
galera::GcsActionSource::process_writeset(void* const              recv_ctx,
                                          const struct gcs_action& act,
                                          bool&                    exit_loop)
{
    assert(act.seqno_g > 0);
    assert(act.seqno_l != GCS_SEQNO_ILL);

    TrxHandleSlavePtr tsp(TrxHandleSlave::New(false, trx_pool_),
                          TrxHandleSlaveDeleter());

    gu_trace(tsp->unserialize<true>(act));
    tsp->set_local(replicator_.source_id() == tsp->source_id());
    gu_trace(replicator_.process_trx(recv_ctx, tsp));
    exit_loop = tsp->exit_loop(); // this is the end of trx lifespan
}

void
galera::GcsActionSource::resend_writeset(const struct gcs_action& act)
{
    assert(act.seqno_g == -EAGAIN);
    assert(act.seqno_l == GCS_SEQNO_ILL);

    ssize_t ret;
    struct gu_buf const sb = { act.buf, act.size };
    GcsI::WriteSetVector v;
    v[0] = sb;

    /* grab send monitor to resend asap */
    while ((ret = gcs_.sendv(v, act.size, act.type, false, true)) == -EAGAIN) {
        usleep(1000);
    }

    if (ret > 0) {
        log_debug << "Local action "
                  << gcs_act_type_to_str(act.type)
                  << " of size " << ret << '/' << act.size
                  << " was resent.";
        /* release source buffer */
        gcache_.free(const_cast<void*>(act.buf));
    }
    else {
        gu_throw_fatal << "Failed to resend action {" << act.buf
                       << ", " << act.size
                       << ", " << gcs_act_type_to_str(act.type)
                       << "}";
    }
}

void galera::GcsActionSource::dispatch(void* const              recv_ctx,
                                       const struct gcs_action& act,
                                       bool&                    exit_loop)
{
    assert(act.buf != 0);
    assert(act.seqno_l > 0 || act.seqno_g == -EAGAIN);

    switch (act.type)
    {
    case GCS_ACT_WRITESET:
        if (act.seqno_g > 0) {
            process_writeset(recv_ctx, act, exit_loop);
        }
        else {
            resend_writeset(act);
        }
        break;
    case GCS_ACT_COMMIT_CUT:
    {
        wsrep_seqno_t seqno;
        gu::unserialize8(act.buf, act.size, 0, seqno);
        assert(seqno >= 0);
        gu_trace(replicator_.process_commit_cut(seqno, act.seqno_l));
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
    case GCS_ACT_VOTE:
    {
        int64_t seqno;
        size_t const off(gu::unserialize8(act.buf, act.size, 0, seqno));
        int64_t code;
        gu::unserialize8(act.buf, act.size, off, code);
        assert(seqno >= 0);
        gu_trace(replicator_.process_vote(seqno, act.seqno_l, code));
        break;
    }
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
                    GCS_ACT_CCHANGE != act.type &&
                    GCS_ACT_VOTE    != act.type &&
                    /* action needs resending */
                    -EAGAIN         != act.seqno_g);

    if (gu_likely(rc > 0 && !skip))
    {
        Release release(act, gcache_);

        if (-EAGAIN != act.seqno_g /* replicated action */)
        {
            ++received_;
            received_bytes_ += rc;
        }
        try { gu_trace(dispatch(recv_ctx, act, exit_loop)); }
        catch (gu::Exception& e)
        {
            log_error << "Failed to process action " << act << ": "
                      << e.what();
            rc = -e.get_errno();
        }
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
