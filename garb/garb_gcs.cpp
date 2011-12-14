/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#include "garb_gcs.hpp"

namespace garb
{

Gcs::Gcs (gu::Config&        gconf,
          const std::string& address,
          const std::string& group) throw (gu::Exception)
:
    closed_ (true),
    gcs_ (gcs_create (reinterpret_cast<gu_config_t*>(&gconf), NULL,
                      GCS_ARBITRATOR_NAME, "", 1, 1))
{
    if (!gcs_)
    {
        gu_throw_fatal << "Failed to create GCS object";
    }

    ssize_t ret = gcs_open (gcs_, group.c_str(), address.c_str());

    if (ret < 0)
    {
        gcs_destroy (gcs_);
        gu_throw_error(-ret) << "Failed to open connection to group";
    }

    closed_ = false;
}

Gcs::~Gcs ()
{
    if (!closed_)
    {
        log_warn << "Destroying non-closed object, bad idea";
        close ();
    }

    gcs_destroy (gcs_);
}

void
Gcs::recv (gcs_action& act) throw (gu::Exception)
{
again:
    ssize_t ret = gcs_recv(gcs_, &act);

    if (gu_unlikely(ret < 0))
    {
        if (-ECANCELED == ret)
        {
            ret = gcs_resume_recv (gcs_);
            if (0 == ret) goto again;
        }

        log_fatal << "Receiving from group failed: " << ret
                  << " (" << strerror(-ret) << ")";
        gu_throw_error(-ret) << "Receiving from group failed";
    }
}

void
Gcs::request_state_transfer (const std::string& request,
                             const std::string& donor) throw (gu::Exception)
{
    gcs_seqno_t order;

    log_info << "Sending state transfer request: '" << request
             << "', size: " << request.length();

again:
    ssize_t ret = gcs_request_state_transfer (gcs_,
                                              request.c_str(),
                                              request.length() + 1 /* \0 */,
                                              donor.c_str(),
                                              &order);
    if (ret < 0)
    {
        if (-EAGAIN == ret)
        {
            usleep (1000000);
            goto again;
        }

        log_fatal << "State transfer request failed: " << ret
                  << " (" << strerror(-ret) << ")";
        gu_throw_error(-ret) << "State transfer request failed";
    }
}

void
Gcs::join (gcs_seqno_t seqno) throw (gu::Exception)
{
    ssize_t ret = gcs_join (gcs_, seqno);

    if (ret < 0)
    {
        log_fatal << "Joining group failed: " << ret
                  << " (" << strerror(-ret) << ")";
        gu_throw_error(-ret) << "Joining group failed";
    }
}

void
Gcs::set_last_applied (gcs_seqno_t seqno) throw()
{
    (void) gcs_set_last_applied(gcs_, seqno);
}

void
Gcs::close ()
{
    if (!closed_)
    {
        ssize_t ret = gcs_close (gcs_);

        if (ret < 0)
        {
            log_error << "Failed to close connection to group";
        }
        else
        {
            closed_ = true;
        }
    }
    else
    {
        log_warn << "Attempt to close a closed connection";
    }
}

} /* namespace garb */
