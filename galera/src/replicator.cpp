//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "replicator.hpp"

namespace galera
{

std::string const Replicator::Param::debug_log = "debug";

void Replicator::register_params(gu::Config& conf)
{
    conf.add(Param::debug_log, "no");
}

const char* const
Replicator::TRIVIAL_SST(WSREP_STATE_TRANSFER_TRIVIAL);

} /* namespace galera */

