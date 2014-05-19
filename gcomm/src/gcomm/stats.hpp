/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_STATS_HPP
#define GCOMM_STATS_HPP

#include <map>
#include <string>

namespace gcomm
{
    enum StatsKey
    {
        S_REPL_LAGTENCY_HIST,
        S_MAX
    };
    const char* stats_key_to_string(StatsKey key);
    typedef std::map<StatsKey, std::string> Stats;
}

#endif // GCOMM_STATS_HPP
