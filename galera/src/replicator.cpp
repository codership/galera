//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#include "replicator.hpp"

namespace galera
{

std::string const Replicator::Param::debug_log = "debug";
#ifdef GU_DBUG_ON
std::string const Replicator::Param::dbug = "dbug";
std::string const Replicator::Param::signal = "signal";
#endif /* GU_DBUG_ON */

void Replicator::register_params(gu::Config& conf)
{
    conf.add(Param::debug_log, "no");
#ifdef GU_DBUG_ON
    conf.add(Param::dbug, "");
    conf.add(Param::signal, "");
#endif /* GU_DBUG_ON */
}

const char* const
Replicator::TRIVIAL_SST(WSREP_STATE_TRANSFER_TRIVIAL);

} /* namespace galera */

