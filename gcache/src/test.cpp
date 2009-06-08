/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include <galerautils.hpp>
#include "GCache.hpp"

using namespace gcache;

int
main (int argc, char* argv[])
{
    int ret = 0;
    std::string fname = "test.cache";

    gu_conf_self_tstamp_on ();
    gu_conf_debug_on ();

    log_info  << "Start";
    log_debug << "DEBUG output enabled";

    if (argc > 1) fname.assign(argv[1]); // take supplied file name if any
    GCache* cache = new GCache (fname, 50);

    log_info  << "";
    log_info  << "...do something...";
    log_info  << "";

    delete cache;

    log_info  << "Exit: " << ret;

    try {
        throw gu::Exception ("My test exception");
    }
    catch (gu::Exception& e) {
        log_info << "Exception caught: " << e.what() << ", errno: "
                 << e.get_errno();
    }

    return ret;
}
