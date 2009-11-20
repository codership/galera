/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/conf.hpp"

static std::string const Delim = ".";

// TCP
std::string const gcomm::Conf::TcpScheme = "tcp";
std::string const gcomm::Conf::TcpNonBlocking =
    "socket" + Delim + "non_blocking";

// GMCast
std::string const gcomm::Conf::GMCastScheme = "gmcast";
std::string const gcomm::Conf::GMCastGroup =
    GMCastScheme + Delim + "group";
std::string const gcomm::Conf::GMCastListenAddr =
    GMCastScheme + Delim + "listen_addr";

// EVS
std::string const gcomm::Conf::EvsScheme = "evs";
std::string const gcomm::Conf::EvsViewForgetTimeout = 
    EvsScheme + Delim + "view_forget_timeout";
std::string const gcomm::Conf::EvsInactiveTimeout = 
    EvsScheme + Delim + "inactive_timeout";
std::string const gcomm::Conf::EvsInactiveCheckPeriod = 
    EvsScheme + Delim + "inactive_check_period";
std::string const gcomm::Conf::EvsConsensusTimeout = 
    EvsScheme + Delim + "consensus_timeout";
std::string const gcomm::Conf::EvsKeepalivePeriod = 
    EvsScheme + Delim + "keepalive_period";
std::string const gcomm::Conf::EvsJoinRetransPeriod =
    EvsScheme + Delim + "join_retrans_period";
std::string const gcomm::Conf::EvsStatsReportPeriod =
    EvsScheme + Delim + "stats_report_period";
std::string const gcomm::Conf::EvsDebugLogMask = 
    EvsScheme + Delim + "debug_log_mask";
std::string const gcomm::Conf::EvsInfoLogMask =
    EvsScheme + Delim + "info_log_mask";
std::string const gcomm::Conf::EvsSendWindow =
    EvsScheme + Delim + "send_window";
std::string const gcomm::Conf::EvsUserSendWindow =
    EvsScheme + Delim + "user_send_window";

// PC
std::string const gcomm::Conf::PcScheme = "pc";
