/* Copyright (C) 2011-2014 Codership Oy <info@codership.com> */

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

    struct RegisterParams
    {
        RegisterParams(gu::Config& cnf)
        {
            gcs_register_params(reinterpret_cast<gu_config_t*>(&cnf));
        }
    }
        params_;

    struct ParseOptions
    {
        ParseOptions(gu::Config& cnf, const std::string& opt)
        {
            cnf.parse(opt);
        }
    }
        parse_;

    Gcs           gcs_;
}; /* RecvLoop */

} /* namespace garb */

#endif /* _GARB_RECV_LOOP_HPP_ */
