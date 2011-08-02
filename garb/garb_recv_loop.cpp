/* Copyright (C) 2011 Codership Oy <info@codership.com> */

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


RecvLoop::RecvLoop (const Config& config) throw (gu::Exception)
    :
    config_(config),
    gconf_ (config_.options()),
    gcs_   (gconf_, config_.address(), config_.group())
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
RecvLoop::loop() throw (gu::Exception)
{
    while (1)
    {
        void*          act = 0;
        size_t         act_size;
        gcs_act_type_t act_type;
        gcs_seqno_t    act_id;

        gcs_.recv (act, act_size, act_type, act_id);

        switch (act_type)
        {
        case GCS_ACT_TORDERED:
            if (gu_unlikely(!(act_id & 127))) /* == report_interval_ of 128 */
            {
                gcs_.set_last_applied (act_id);
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
                (reinterpret_cast<gcs_act_conf_t*>(act));

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

        if (act_size > 0)
        {
            assert (act);
            free (act);
        }
    }
}

} /* namespace garb */
