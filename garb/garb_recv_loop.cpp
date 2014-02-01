/* Copyright (C) 2011-2014 Codership Oy <info@codership.com> */

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
    gcs_   (gconf_, config_.name(), config_.address(), config_.group())
{
    /* set up signal handlers */
    global_gcs = &gcs_;

    struct sigaction sa;

    memset (&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;

    if (sigaction (SIGTERM, &sa, NULL))
    {
        gu_throw_error(errno) << "Falied to install signal hadler for signal "
                              << "SIGTERM";
    }

    if (sigaction (SIGINT, &sa, NULL))
    {
        gu_throw_error(errno) << "Falied to install signal hadler for signal "
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
        case GCS_ACT_TORDERED:
            if (gu_unlikely(!(act.seqno_g & 127)))
                /* == report_interval_ of 128 */
            {
                gcs_.set_last_applied (act.seqno_g);
            }
            break;
        case GCS_ACT_COMMIT_CUT:
            break;
        case GCS_ACT_STATE_REQ:
            gcs_.join (-ENOSYS); /* we can't donate state */
            break;
        case GCS_ACT_CONF:
        {
            const gcs_act_conf_t* const cc
                (reinterpret_cast<const gcs_act_conf_t*>(act.buf));

            if (cc->conf_id > 0) /* PC */
            {
                if (GCS_NODE_STATE_PRIM == cc->my_state)
                {
                    gcs_.request_state_transfer (config_.sst(),config_.donor());
                    gcs_.join(cc->seqno);
                }
            }
            else if (cc->memb_num == 0) // SELF-LEAVE after closing connection
            {
                log_info << "Exiting main loop";
                return;
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
        case GCS_ACT_SERVICE:
        case GCS_ACT_ERROR:
        case GCS_ACT_UNKNOWN:
            break;
        }

        if (act.buf)
        {
            free (const_cast<void*>(act.buf));
        }
    }
}

} /* namespace garb */
