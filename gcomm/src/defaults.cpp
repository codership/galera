/*
 * Copyright (C) 2012-2014 Codership Oy <info@codership.com>
 */

#include "defaults.hpp"

#include "gcomm/common.hpp"

namespace gcomm
{
#ifdef HAVE_ASIO_HPP
    std::string const Defaults::ProtonetBackend         = "asio";
#else
#error "Only asio protonet backend is currently supported"
#endif /* HAVE_ASIO_HPP */

    std::string const Defaults::ProtonetVersion         = "0";
    std::string const Defaults::SocketChecksum          = "2";
    std::string const Defaults::SocketRecvBufSize       = "212992";
    std::string const Defaults::GMCastVersion           = "0";
    std::string const Defaults::GMCastTcpPort           = BASE_PORT_DEFAULT;
    std::string const Defaults::GMCastSegment           = "0";
    std::string const Defaults::GMCastTimeWait          = "PT5S";
    std::string const Defaults::GMCastPeerTimeout       = "PT3S";
    std::string const Defaults::EvsViewForgetTimeout    = "PT24H";
    std::string const Defaults::EvsViewForgetTimeoutMin = "PT1S";
    std::string const Defaults::EvsInactiveCheckPeriod  = "PT0.5S";
    std::string const Defaults::EvsSuspectTimeout       = "PT5S";
    std::string const Defaults::EvsSuspectTimeoutMin    = "PT0.1S";
    std::string const Defaults::EvsInactiveTimeout      = "PT15S";
    std::string const Defaults::EvsInactiveTimeoutMin   = "PT0.1S";
    std::string const Defaults::EvsRetransPeriod        = "PT1S";
    std::string const Defaults::EvsRetransPeriodMin     = "PT0.1S";
    std::string const Defaults::EvsJoinRetransPeriod    = "PT1S";
    std::string const Defaults::EvsStatsReportPeriod    = "PT1M";
    std::string const Defaults::EvsStatsReportPeriodMin = "PT1S";
    std::string const Defaults::EvsSendWindow           = "4";
    std::string const Defaults::EvsSendWindowMin        = "1";
    std::string const Defaults::EvsUserSendWindow       = "2";
    std::string const Defaults::EvsUserSendWindowMin    = "1";
    std::string const Defaults::EvsMaxInstallTimeouts   = "3";
    std::string const Defaults::EvsDelayMargin          = "PT1S";
    std::string const Defaults::EvsDelayedKeepPeriod    = "PT30S";
    std::string const Defaults::EvsAutoEvict            = "0";
    std::string const Defaults::EvsVersion              = "1";
    std::string const Defaults::PcAnnounceTimeout       = "PT3S";
    std::string const Defaults::PcChecksum              = "false";
    std::string const Defaults::PcIgnoreQuorum          = "false";
    std::string const Defaults::PcIgnoreSb              = PcIgnoreQuorum;
    std::string const Defaults::PcNpvo                  = "false";
    std::string const Defaults::PcVersion               = "0";
    std::string const Defaults::PcWaitPrim              = "true";
    std::string const Defaults::PcWaitPrimTimeout       = "PT30S";
    std::string const Defaults::PcWeight                = "1";
    std::string const Defaults::PcRecovery              = "true";
}
