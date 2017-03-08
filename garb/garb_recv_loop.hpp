/* Copyright (C) 2011-2016 Codership Oy <info@codership.com> */

#ifndef _GARB_RECV_LOOP_HPP_
#define _GARB_RECV_LOOP_HPP_

#include "garb_gcs.hpp"
#include "garb_config.hpp"

#include <gu_throw.hpp>
#include <gu_asio.hpp>

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
            gu::ssl_register_params(cnf);
            if (gcs_register_params(reinterpret_cast<gu_config_t*>(&cnf)))
            {
                gu_throw_fatal << "Error initializing GCS parameters";
            }
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

    Gcs gcs_;

    gu::UUID    uuid_;
    gu::seqno_t seqno_;
    int         proto_;

}; /* RecvLoop */

} /* namespace garb */

#endif /* _GARB_RECV_LOOP_HPP_ */
