/* Copyright (C) 2011 Codership Oy <info@codership.com> */

#ifndef _GARB_RECV_LOOP_HPP_
#define _GARB_RECV_LOOP_HPP_

#include "garb_gcs.hpp"
#include "garb_config.hpp"

#include <galerautils.hpp>

#include <pthread.h>

namespace garb
{

class RecvLoop
{
public:

    RecvLoop (const Config&);

    ~RecvLoop () {}

private:

    void loop();

    const Config& config_;
    gu::Config    gconf_;
    Gcs           gcs_;
}; /* RecvLoop */

} /* namespace garb */

#endif /* _GARB_RECV_LOOP_HPP_ */
