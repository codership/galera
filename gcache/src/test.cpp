/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 */

#include "Logger.hpp"
#include "GCache.hpp"

using namespace gcache;

int
main (int argc, char* argv[])
{
    int ret = 0;
    std::string fname = "test.cache";

    Logger::enable_tstamp (true);
    Logger::enable_debug  (true);

    log_info  << "Start";
    log_debug << "DEBUG output enabled";

    if (argc > 1) fname.assign(argv[1]); // take supplied file name if any
    GCache* cache = new GCache (fname);

    delete cache;

    log_info  << "Exit: " << ret;

    return ret;
}
