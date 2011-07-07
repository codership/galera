/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "defaults.hpp"

using std::string;


std::string const gcomm::Defaults::GMCastTcpPort            = "4567";
std::string const gcomm::Defaults::EvsViewForgetTimeout     = "PT5M";
std::string const gcomm::Defaults::EvsViewForgetTimeoutMin  = "PT1S";
std::string const gcomm::Defaults::EvsInactiveCheckPeriod   = "PT1S";
std::string const gcomm::Defaults::EvsSuspectTimeout        = "PT5S";
std::string const gcomm::Defaults::EvsSuspectTimeoutMin     = "PT0.1S";
std::string const gcomm::Defaults::EvsInactiveTimeout       = "PT15S";
std::string const gcomm::Defaults::EvsInactiveTimeoutMin    = "PT0.1S";
std::string const gcomm::Defaults::EvsRetransPeriod         = "PT1S";
std::string const gcomm::Defaults::EvsRetransPeriodMin      = "PT0.1S";
std::string const gcomm::Defaults::EvsJoinRetransPeriod     = "PT0.3S";
std::string const gcomm::Defaults::EvsStatsReportPeriod     = "PT1M";
std::string const gcomm::Defaults::EvsStatsReportPeriodMin  = "PT1S";
std::string const gcomm::Defaults::EvsSendWindow            = "4";
std::string const gcomm::Defaults::EvsSendWindowMin         = "1";
std::string const gcomm::Defaults::EvsUserSendWindow        = "2";
std::string const gcomm::Defaults::EvsUserSendWindowMin     = "1";
std::string const gcomm::Defaults::EvsMaxInstallTimeouts    = "1";
