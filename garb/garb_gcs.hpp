/* Copyright (C) 2011 Codership Oy <info@codership.com> */

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
         const std::string& address,
         const std::string& group)
        throw (gu::Exception);

    ~Gcs ();

    void recv (void*&          act,
               size_t&         act_size,
               gcs_act_type_t& act_type,
               gcs_seqno_t&    act_id)
        throw (gu::Exception);

    void request_state_transfer (const std::string& request,
                                 const std::string& donor)
        throw (gu::Exception);

    void join (gcs_seqno_t)
        throw (gu::Exception);

    void set_last_applied(gcs_seqno_t) throw();

    void close ();

private:

    bool        closed_;
    gcs_conn_t* gcs_;

    Gcs (const Gcs&);
    Gcs& operator= (const Gcs&);

}; /* class Gcs */

} /* namespace garb */

#endif /* _GARB_GCS_HPP_ */
