/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 */

#include "defaults.hpp"

#include "gcomm/common.hpp"

namespace gcomm
{
    std::string const Defaults::GMCastTcpPort           = BASE_PORT_DEFAULT;
    std::string const Defaults::EvsViewForgetTimeout    = "PT5M";
    std::string const Defaults::EvsViewForgetTimeoutMin = "PT1S";
    std::string const Defaults::EvsInactiveCheckPeriod  = "PT0.5S";
    std::string const Defaults::EvsSuspectTimeout       = "PT5S";
    std::string const Defaults::EvsSuspectTimeoutMin    = "PT0.1S";
    std::string const Defaults::EvsInactiveTimeout      = "PT15S";
    std::string const Defaults::EvsInactiveTimeoutMin   = "PT0.1S";
    std::string const Defaults::EvsRetransPeriod        = "PT1S";
    std::string const Defaults::EvsRetransPeriodMin     = "PT0.1S";
    std::string const Defaults::EvsJoinRetransPeriod    = "PT0.3S";
    std::string const Defaults::EvsStatsReportPeriod    = "PT1M";
    std::string const Defaults::EvsStatsReportPeriodMin = "PT1S";
    std::string const Defaults::EvsSendWindow           = "4";
    std::string const Defaults::EvsSendWindowMin        = "1";
    std::string const Defaults::EvsUserSendWindow       = "2";
    std::string const Defaults::EvsUserSendWindowMin    = "1";
    std::string const Defaults::EvsMaxInstallTimeouts   = "1";
    std::string const Defaults::PcAnnounceTimeout       = "PT3S";
}
