/* Copyright (C) 2011-2013 Codership Oy <info@codership.com> */

#ifndef _GARB_GCS_HPP_
#define _GARB_GCS_HPP_

#include <gcs.h>
#include <galerautils.hpp>

namespace garb
{

class Gcs
{
public:

    Gcs (gu::Config&        conf,
         const std::string& name,
         const std::string& address,
         const std::string& group);

    ~Gcs ();

    void recv (gcs_action& act);

    void request_state_transfer (const std::string& request,
                                 const std::string& donor);

    void join (gcs_seqno_t);

    void set_last_applied(gcs_seqno_t);

    void close ();

private:

    bool        closed_;
    gcs_conn_t* gcs_;

    Gcs (const Gcs&);
    Gcs& operator= (const Gcs&);

}; /* class Gcs */

} /* namespace garb */

#endif /* _GARB_GCS_HPP_ */
