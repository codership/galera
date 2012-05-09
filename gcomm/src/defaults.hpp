/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_DEFAULTS_HPP
#define GCOMM_DEFAULTS_HPP

#include <string>

namespace gcomm
{
    struct Defaults
    {
        static std::string const GMCastTcpPort            ;
        static std::string const EvsViewForgetTimeout     ;
        static std::string const EvsViewForgetTimeoutMin  ;
        static std::string const EvsInactiveCheckPeriod   ;
        static std::string const EvsSuspectTimeout        ;
        static std::string const EvsSuspectTimeoutMin     ;
        static std::string const EvsInactiveTimeout       ;
        static std::string const EvsInactiveTimeoutMin    ;
        static std::string const EvsInstallTimeout        ;
        static std::string const EvsRetransPeriod         ;
        static std::string const EvsRetransPeriodMin      ;
        static std::string const EvsJoinRetransPeriod     ;
        static std::string const EvsStatsReportPeriod     ;
        static std::string const EvsStatsReportPeriodMin  ;
        static std::string const EvsSendWindow            ;
        static std::string const EvsSendWindowMin         ;
        static std::string const EvsUserSendWindow        ;
        static std::string const EvsUserSendWindowMin     ;
        static std::string const EvsMaxInstallTimeouts    ;
        static std::string const PcAnnounceTimeout        ;
        static std::string const PcWaitPrim               ;
        static std::string const PcWaitPrimTimeout        ;
    };
}

#endif // GCOMM_DEFAULTS_HPP
