/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#include "garb_gcs.hpp"

namespace garb
{

Gcs::Gcs (gu::Config&        gconf,
          const std::string& address,
          const std::string& group) throw (gu::Exception)
:
    closed_ (true),
    gcs_ (gcs_create (GCS_ARBITRATOR_NAME, "", &gconf, 1, 1, NULL))
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
Gcs::recv (void*&          act,
           size_t&         act_size,
           gcs_act_type_t& act_type,
           gcs_seqno_t&    act_id) throw (gu::Exception)
{
    gcs_seqno_t seqno_l;

    ssize_t ret = gcs_recv(gcs_, &act, &act_size, &act_type, &act_id, &seqno_l);

    if (ret < 0)
    {
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

    ssize_t ret = gcs_request_state_transfer (gcs_,
                                              request.c_str(),
                                              request.length() + 1 /* \0 */,
                                              donor.c_str(),
                                              &order);
    if (ret < 0)
    {
        gu_throw_error(-ret) << "State transfer request failed";
    }
}

void
Gcs::join (gcs_seqno_t seqno) throw (gu::Exception)
{
    ssize_t ret = gcs_join (gcs_, seqno);

    if (ret < 0)
    {
        gu_throw_error(-ret) << "Join group failed";
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
