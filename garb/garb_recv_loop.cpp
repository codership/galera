/* Copyright (C) 2011-2016 Codership Oy <info@codership.com> */

#include "garb_recv_loop.hpp"

#include <signal.h>

namespace garb
{

static Gcs*
global_gcs(0);

void
signal_handler (int signum)
{
    log_info << "Received signal " << signum;
    global_gcs->close();
}


RecvLoop::RecvLoop (const Config& config)
    :
    config_(config),
    gconf_ (),
    params_(gconf_),
    parse_ (gconf_, config_.options()),
    gcs_   (gconf_, config_.name(), config_.address(), config_.group()),
    uuid_  (GU_UUID_NIL),
    seqno_ (GCS_SEQNO_ILL),
    proto_ (0)
{
    /* set up signal handlers */
    global_gcs = &gcs_;

    struct sigaction sa;

    memset (&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;

    if (sigaction (SIGTERM, &sa, NULL))
    {
        gu_throw_error(errno) << "Falied to install signal handler for signal "
                              << "SIGTERM";
    }

    if (sigaction (SIGINT, &sa, NULL))
    {
        gu_throw_error(errno) << "Falied to install signal handler for signal "
                              << "SIGINT";
    }

    loop();
}

void
RecvLoop::loop()
{
    while (1)
    {
        gcs_action act;

        gcs_.recv (act);

        switch (act.type)
        {
        case GCS_ACT_WRITESET:
            seqno_ = act.seqno_g;
            if (gu_unlikely(proto_ == 0 && !(seqno_ & 127)))
                /* report_interval_ of 128 in old protocol */
            {
                gcs_.set_last_applied (gu::GTID(uuid_, seqno_));
            }
            break;
        case GCS_ACT_COMMIT_CUT:
            break;
        case GCS_ACT_STATE_REQ:
            /* we can't donate state */
            gcs_.join (gu::GTID(uuid_, seqno_),-ENOSYS);
            break;
        case GCS_ACT_CCHANGE:
        {
            gcs_act_cchange const cc(act.buf, act.size);

            if (cc.conf_id > 0) /* PC */
            {
                int const my_idx(act.seqno_g);
                assert(my_idx >= 0);

                gcs_node_state const my_state(cc.memb[my_idx].state_);

                if (GCS_NODE_STATE_PRIM == my_state)
                {
                    uuid_  = cc.uuid;
                    seqno_ = cc.seqno;
                    gcs_.request_state_transfer (config_.sst(),config_.donor());
                    gcs_.join(gu::GTID(cc.uuid, cc.seqno), 0);
                }

                proto_ = gcs_.proto_ver();
            }
            else
            {
                if (cc.memb.size() == 0) // SELF-LEAVE after closing connection
                {
                    log_info << "Exiting main loop";
                    return;
                }
                uuid_  = GU_UUID_NIL;
                seqno_ = GCS_SEQNO_ILL;
            }

            if (config_.sst() != Config::DEFAULT_SST)
            {
                // we requested custom SST, so we're done here
                gcs_.close();
            }

            break;
        }
        case GCS_ACT_JOIN:
        case GCS_ACT_SYNC:
        case GCS_ACT_FLOW:
        case GCS_ACT_VOTE:
        case GCS_ACT_SERVICE:
        case GCS_ACT_ERROR:
        case GCS_ACT_UNKNOWN:
            break;
        }

        if (act.buf)
        {
            ::free(const_cast<void*>(act.buf));
        }
    }
}

} /* namespace garb */
