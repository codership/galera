/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_DEFAULTS_HPP
#define GCOMM_DEFAULTS_HPP

#include <string>
#include "gu_config.hpp"

namespace gcomm
{
    struct Defaults
    {
        static std::string const ProtonetBackend          ;
        static std::string const ProtonetVersion          ;
        static std::string const SocketChecksum           ;
        static std::string const SocketRecvBufSize        ;
        static std::string const SocketSendBufSize        ;
        static std::string const GMCastVersion            ;
        static std::string const GMCastTcpPort            ;
        static std::string const GMCastSegment            ;
        static std::string const GMCastTimeWait           ;
        static std::string const GMCastPeerTimeout        ;
        static std::string const EvsViewForgetTimeout     ;
        static std::string const EvsViewForgetTimeoutMin  ;
        static std::string const EvsInactiveCheckPeriod   ;
        static std::string const EvsSuspectTimeout        ;
        static std::string const EvsSuspectTimeoutMin     ;
        static std::string const EvsInactiveTimeout       ;
        static std::string const EvsInactiveTimeoutMin    ;
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
        static std::string const EvsDelayMargin           ;
        static std::string const EvsDelayedKeepPeriod     ;
        static std::string const EvsAutoEvict             ;
        static std::string const EvsVersion               ;
        static std::string const PcAnnounceTimeout        ;
        static std::string const PcChecksum               ;
        static std::string const PcIgnoreQuorum           ;
        static std::string const PcIgnoreSb               ;
        static std::string const PcNpvo                   ;
        static std::string const PcVersion                ;
        static std::string const PcWaitPrim               ;
        static std::string const PcWaitPrimTimeout        ;
        static std::string const PcWeight                 ;
        static std::string const PcRecovery               ;
    };

    struct Flags
    {
        static const int BaseHost = gu::Config::Flag::read_only;
        static const int BasePort = gu::Config::Flag::read_only |
                                    gu::Config::Flag::type_integer;

        static const int ProtonetBackend = gu::Config::Flag::read_only;
        static const int ProtonetVersion = gu::Config::Flag::read_only;

        // Hidden because not documented / does not seem to be used?
        static const int TcpNonBlocking = gu::Config::Flag::hidden;
        static const int SocketChecksum = gu::Config::Flag::read_only |
                                          gu::Config::Flag::type_integer;
        static const int SocketRecvBufSize = 0;
        static const int SocketSendBufSize = 0;

        static const int GMCastVersion     = gu::Config::Flag::read_only;
        static const int GMCastGroup       = gu::Config::Flag::read_only;
        static const int GMCastListenAddr  = gu::Config::Flag::read_only;
        static const int GMCastMCastAddr   = gu::Config::Flag::read_only;
        // Hidden because undocumented
        static const int GMCastMCastPort   = gu::Config::Flag::hidden |
                                             gu::Config::Flag::read_only |
                                             gu::Config::Flag::type_integer;
        static const int GMCastMCastTTL    = gu::Config::Flag::read_only |
                                             gu::Config::Flag::type_integer;
        static const int GMCastTimeWait    = gu::Config::Flag::read_only |
                                             gu::Config::Flag::type_duration;
        static const int GMCastPeerTimeout = gu::Config::Flag::read_only |
                                             gu::Config::Flag::type_duration;;
        // Hidden because undocumented
        static const int GMCastMaxInitialReconnectAttempts =
            gu::Config::Flag::hidden | gu::Config::Flag::type_integer;
        static const int GMCastPeerAddr    = 0;
        // Hidden because undocumented, potentially dangerous
        static const int GMCastIsolate     = gu::Config::Flag::hidden |
            gu::Config::Flag::type_integer;
        static const int GMCastSegment     = gu::Config::Flag::read_only |
                                             gu::Config::Flag::type_integer;

        static const int EvsVersion           = gu::Config::Flag::read_only;
        static const int EvsViewForgetTimeout = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_duration;
        static const int EvsSuspectTimeout    = gu::Config::Flag::type_duration;
        static const int EvsInactiveTimeout   = gu::Config::Flag::type_duration;
        static const int EvsInactiveCheckPeriod = gu::Config::Flag::type_duration;
        static const int EvsInstallTimeout    = gu::Config::Flag::type_duration;
        static const int EvsKeepalivePeriod   = gu::Config::Flag::type_duration;
        static const int EvsJoinRetransPeriod = gu::Config::Flag::type_duration;
        static const int EvsStatsReportPeriod = gu::Config::Flag::type_duration;
        static const int EvsDebugLogMask      = 0;
        static const int EvsInfoLogMask       = 0;
        static const int EvsSendWindow        = gu::Config::Flag::type_integer;
        static const int EvsUserSendWindow    = gu::Config::Flag::type_integer;
        static const int EvsUseAggregate      = gu::Config::Flag::type_bool;
        static const int EvsCausalKeepalivePeriod = gu::Config::Flag::type_duration;
        static const int EvsMaxInstallTimeouts = gu::Config::Flag::type_integer;
        static const int EvsDelayMargin       = gu::Config::Flag::type_duration;
        static const int EvsDelayedKeepPeriod = gu::Config::Flag::type_duration;
        static const int EvsEvict             = 0;
        static const int EvsAutoEvict         = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_bool;

        static const int PcVersion            = gu::Config::Flag::read_only;
        static const int PcIgnoreSb           = gu::Config::Flag::type_bool;
        static const int PcIgnoreQuorum       = gu::Config::Flag::type_bool;
        static const int PcChecksum           = gu::Config::Flag::type_bool;
        static const int PcAnnounceTimeout    = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_duration;
        static const int PcLinger             = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_duration;
        static const int PcNpvo               = gu::Config::Flag::type_bool;
        static const int PcBootstrap          = gu::Config::Flag::type_bool;
        static const int PcWaitPrim           = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_bool;
        static const int PcWaitPrimTimeout    = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_duration;
        static const int PcWeight             = gu::Config::Flag::type_integer;
        static const int PcRecovery           = gu::Config::Flag::read_only |
                                                gu::Config::Flag::type_bool;
    };
}

#endif // GCOMM_DEFAULTS_HPP
