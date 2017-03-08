/*
 * Copyright (C) 2011-2016 Codership Oy <info@codership.com>
 */

#include "garb_gcs.hpp"

namespace garb
{

static int const REPL_PROTO_VER(127);
static int const APPL_PROTO_VER(127);

Gcs::Gcs (gu::Config&        gconf,
          const std::string& name,
          const std::string& address,
          const std::string& group)
:
    closed_ (true),
    gcs_ (gcs_create (reinterpret_cast<gu_config_t*>(&gconf),
                      NULL,
                      name.c_str(),
                      "",
                      REPL_PROTO_VER, APPL_PROTO_VER))
{
    if (!gcs_)
    {
        gu_throw_fatal << "Failed to create GCS object";
    }

    ssize_t ret = gcs_open (gcs_, group.c_str(), address.c_str(), false);

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
Gcs::recv (gcs_action& act)
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
                             const std::string& donor)
{
    gcs_seqno_t order;

    log_info << "Sending state transfer request: '" << request
             << "', size: " << request.length();

    /* Need to substitute the first ':' for \0 */

    ssize_t req_len = request.length() + 1 /* \0 */;
    char* const req_str(reinterpret_cast<char*>(::malloc(
                        req_len + 1 /* potentially need one more \0 */)));
    // cppcheck-suppress nullPointer
    if (!req_str)
    {
        gu_throw_error (ENOMEM) << "Cannot allocate " << req_len
                                << " bytes for state transfer request";
    }

    ::strcpy(req_str, request.c_str());
    char* column_ptr = ::strchr(req_str, ':');

    if (column_ptr)
    {
        *column_ptr = '\0';
    }
    else /* append an empty string */
    {
        req_str[req_len] = '\0';
        req_len++;
    }

    ssize_t ret;
    do
    {
        gu_uuid_t ist_uuid = {{0, }};
        gcs_seqno_t ist_seqno = GCS_SEQNO_ILL;
        // for garb we use the lowest str_version.
        ret = gcs_request_state_transfer (gcs_, 0, req_str, req_len,
                                          donor.c_str(),
                                          gu::GTID(ist_uuid, ist_seqno), order);
    }
    while (-EAGAIN == ret && (usleep(1000000), true));

    free (req_str);

    if (ret < 0)
    {
        log_fatal << "State transfer request failed: " << ret
                  << " (" << strerror(-ret) << ")";
        gu_throw_error(-ret) << "State transfer request failed";
    }
}

void
Gcs::join (const gu::GTID& gtid, int const code)
{
    ssize_t const ret(gcs_join (gcs_, gtid, code));

    if (ret < 0)
    {
        log_fatal << "Joining group failed: " << ret
                  << " (" << strerror(-ret) << ")";
        gu_throw_error(-ret) << "Joining group failed";
    }
}

void
Gcs::set_last_applied (const gu::GTID& gtid)
{
    (void) gcs_set_last_applied(gcs_, gtid);
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
