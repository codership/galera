/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 *
 */

#include "GCache.hpp"

#include <gu_logger.hpp>
#include <gu_exception.hpp>

using namespace gcache;

#define TEST_CACHE "test.cache"

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
    gu::Config conf;
    GCache::register_params(conf);
    conf.parse("gcache.name = " TEST_CACHE "; gcache.size = 16K");
    GCache* cache = new GCache (conf, "");

    log_info  << "";
    log_info  << "...do something...";
    log_info  << "";

    delete cache;
    ::unlink(TEST_CACHE);

    log_info  << "Exit: " << ret;

    try {
        throw gu::Exception ("My test exception", EINVAL);
    }
    catch (gu::Exception& e) {
        log_info << "Exception caught: " << e.what() << ", errno: "
                 << e.get_errno();
    }

    return ret;
}
