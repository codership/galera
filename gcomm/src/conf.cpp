/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "gcomm/conf.hpp"
#include "defaults.hpp"
#include "common.h"

#include <limits>

static std::string const Delim = ".";

// Protonet
std::string const gcomm::Conf::ProtonetBackend("protonet.backend");
std::string const gcomm::Conf::ProtonetVersion("protonet.version");

// TCP
static std::string const SocketPrefix("socket" + Delim);
std::string const gcomm::Conf::TcpNonBlocking =
    SocketPrefix + "non_blocking";
std::string const gcomm::Conf::SocketChecksum =
    SocketPrefix + "checksum";
std::string const gcomm::Conf::SocketRecvBufSize =
    SocketPrefix + "recv_buf_size";
std::string const gcomm::Conf::SocketSendBufSize =
    SocketPrefix + "send_buf_size";

// GMCast
std::string const gcomm::Conf::GMCastScheme = "gmcast";
static std::string const GMCastPrefix(gcomm::Conf::GMCastScheme + Delim);
std::string const gcomm::Conf::GMCastVersion =
    GMCastPrefix + "version";
std::string const gcomm::Conf::GMCastGroup =
    GMCastPrefix + "group";
std::string const gcomm::Conf::GMCastListenAddr =
    GMCastPrefix + "listen_addr";
std::string const gcomm::Conf::GMCastMCastAddr =
    GMCastPrefix + "mcast_addr";
std::string const gcomm::Conf::GMCastMCastPort =
    GMCastPrefix + "mcast_port";
std::string const gcomm::Conf::GMCastMCastTTL =
    GMCastPrefix + "mcast_ttl";
std::string const gcomm::Conf::GMCastTimeWait =
    GMCastPrefix + "time_wait";
std::string const gcomm::Conf::GMCastPeerTimeout =
    GMCastPrefix + "peer_timeout";
std::string const gcomm::Conf::GMCastMaxInitialReconnectAttempts =
    GMCastPrefix + "mira";
std::string const gcomm::Conf::GMCastPeerAddr =
    GMCastPrefix + "peer_addr";
std::string const gcomm::Conf::GMCastIsolate =
    GMCastPrefix + "isolate";
std::string const gcomm::Conf::GMCastSegment =
    GMCastPrefix + "segment";

// EVS
std::string const gcomm::Conf::EvsScheme = "evs";
static std::string const EvsPrefix(gcomm::Conf::EvsScheme + Delim);
std::string const gcomm::Conf::EvsVersion =
    EvsPrefix + "version";
std::string const gcomm::Conf::EvsViewForgetTimeout =
    EvsPrefix + "view_forget_timeout";
std::string const gcomm::Conf::EvsInactiveTimeout =
    EvsPrefix + "inactive_timeout";
std::string const gcomm::Conf::EvsSuspectTimeout =
    EvsPrefix + "suspect_timeout";
std::string const gcomm::Conf::EvsInactiveCheckPeriod =
    EvsPrefix + "inactive_check_period";
std::string const gcomm::Conf::EvsInstallTimeout =
    EvsPrefix + "install_timeout";
std::string const gcomm::Conf::EvsKeepalivePeriod =
    EvsPrefix + "keepalive_period";
std::string const gcomm::Conf::EvsJoinRetransPeriod =
    EvsPrefix + "join_retrans_period";
std::string const gcomm::Conf::EvsStatsReportPeriod =
    EvsPrefix + "stats_report_period";
std::string const gcomm::Conf::EvsDebugLogMask =
    EvsPrefix + "debug_log_mask";
std::string const gcomm::Conf::EvsInfoLogMask =
    EvsPrefix + "info_log_mask";
std::string const gcomm::Conf::EvsSendWindow =
    EvsPrefix + "send_window";
std::string const gcomm::Conf::EvsUserSendWindow =
    EvsPrefix + "user_send_window";
std::string const gcomm::Conf::EvsUseAggregate =
    EvsPrefix + "use_aggregate";
std::string const gcomm::Conf::EvsCausalKeepalivePeriod =
    EvsPrefix + "causal_keepalive_period";
std::string const gcomm::Conf::EvsMaxInstallTimeouts =
    EvsPrefix + "max_install_timeouts";
std::string const gcomm::Conf::EvsDelayMargin =
    EvsPrefix + "delay_margin";
std::string const gcomm::Conf::EvsDelayedKeepPeriod =
    EvsPrefix + "delayed_keep_period";
std::string const gcomm::Conf::EvsEvict =
    EvsPrefix + "evict";
std::string const gcomm::Conf::EvsAutoEvict =
    EvsPrefix + "auto_evict";

// PC
std::string const gcomm::Conf::PcScheme = "pc";
static std::string const PcPrefix(gcomm::Conf::PcScheme + Delim);
std::string const gcomm::Conf::PcVersion = PcPrefix + "version";
std::string const gcomm::Conf::PcIgnoreSb = PcPrefix + "ignore_sb";
std::string const gcomm::Conf::PcIgnoreQuorum =
    PcPrefix + "ignore_quorum";
std::string const gcomm::Conf::PcChecksum = PcPrefix + "checksum";
std::string const gcomm::Conf::PcLinger = PcPrefix + "linger";
std::string const gcomm::Conf::PcAnnounceTimeout =
    PcPrefix + "announce_timeout";
std::string const gcomm::Conf::PcNpvo = PcPrefix + "npvo";
std::string const gcomm::Conf::PcBootstrap = PcPrefix + "bootstrap";
std::string const gcomm::Conf::PcWaitPrim = PcPrefix + "wait_prim";
std::string const gcomm::Conf::PcWaitPrimTimeout =
    PcPrefix + "wait_prim_timeout";
std::string const gcomm::Conf::PcWeight = PcPrefix + "weight";
std::string const gcomm::Conf::PcRecovery = PcPrefix + "recovery";

void
gcomm::Conf::register_params(gu::Config& cnf)
{
#define GCOMM_CONF_ADD(_x_) cnf.add(_x_);
#define GCOMM_CONF_ADD_DEFAULT(_x_) cnf.add(_x_, Defaults::_x_);

    GCOMM_CONF_ADD (COMMON_BASE_HOST_KEY);
    GCOMM_CONF_ADD (COMMON_BASE_PORT_KEY);

    GCOMM_CONF_ADD_DEFAULT(ProtonetBackend);
    GCOMM_CONF_ADD_DEFAULT(ProtonetVersion);

    GCOMM_CONF_ADD        (TcpNonBlocking);
    GCOMM_CONF_ADD_DEFAULT(SocketChecksum);
    GCOMM_CONF_ADD_DEFAULT(SocketRecvBufSize);
    GCOMM_CONF_ADD_DEFAULT(SocketSendBufSize);

    GCOMM_CONF_ADD_DEFAULT(GMCastVersion);
    GCOMM_CONF_ADD        (GMCastGroup);
    GCOMM_CONF_ADD        (GMCastListenAddr);
    GCOMM_CONF_ADD        (GMCastMCastAddr);
    GCOMM_CONF_ADD        (GMCastMCastPort);
    GCOMM_CONF_ADD        (GMCastMCastTTL);
    GCOMM_CONF_ADD        (GMCastMCastAddr);
    GCOMM_CONF_ADD        (GMCastTimeWait);
    GCOMM_CONF_ADD        (GMCastPeerTimeout);
    GCOMM_CONF_ADD        (GMCastMaxInitialReconnectAttempts);
    GCOMM_CONF_ADD        (GMCastPeerAddr);
    GCOMM_CONF_ADD        (GMCastIsolate);
    GCOMM_CONF_ADD_DEFAULT(GMCastSegment);

    GCOMM_CONF_ADD        (EvsVersion);
    GCOMM_CONF_ADD_DEFAULT(EvsViewForgetTimeout);
    GCOMM_CONF_ADD_DEFAULT(EvsSuspectTimeout);
    GCOMM_CONF_ADD_DEFAULT(EvsInactiveTimeout);
    GCOMM_CONF_ADD_DEFAULT(EvsInactiveCheckPeriod);
    GCOMM_CONF_ADD        (EvsInstallTimeout);
    GCOMM_CONF_ADD        (EvsKeepalivePeriod);
    GCOMM_CONF_ADD_DEFAULT(EvsJoinRetransPeriod);
    GCOMM_CONF_ADD_DEFAULT(EvsStatsReportPeriod);
    GCOMM_CONF_ADD        (EvsDebugLogMask);
    GCOMM_CONF_ADD        (EvsInfoLogMask);
    GCOMM_CONF_ADD_DEFAULT(EvsSendWindow);
    GCOMM_CONF_ADD_DEFAULT(EvsUserSendWindow);
    GCOMM_CONF_ADD        (EvsUseAggregate);
    GCOMM_CONF_ADD        (EvsCausalKeepalivePeriod);
    GCOMM_CONF_ADD_DEFAULT(EvsMaxInstallTimeouts);
    GCOMM_CONF_ADD_DEFAULT(EvsDelayMargin);
    GCOMM_CONF_ADD_DEFAULT(EvsDelayedKeepPeriod);
    GCOMM_CONF_ADD        (EvsEvict);
    GCOMM_CONF_ADD_DEFAULT(EvsAutoEvict);

    GCOMM_CONF_ADD_DEFAULT(PcVersion);
    GCOMM_CONF_ADD_DEFAULT(PcIgnoreSb);
    GCOMM_CONF_ADD_DEFAULT(PcIgnoreQuorum);
    GCOMM_CONF_ADD_DEFAULT(PcChecksum);
    GCOMM_CONF_ADD_DEFAULT(PcAnnounceTimeout);
    GCOMM_CONF_ADD        (PcLinger);
    GCOMM_CONF_ADD_DEFAULT(PcNpvo);
    GCOMM_CONF_ADD        (PcBootstrap);
    GCOMM_CONF_ADD_DEFAULT(PcWaitPrim);
    GCOMM_CONF_ADD_DEFAULT(PcWaitPrimTimeout);
    GCOMM_CONF_ADD_DEFAULT(PcWeight);
    GCOMM_CONF_ADD_DEFAULT(PcRecovery);

#undef GCOMM_CONF_ADD
#undef GCOMM_CONF_ADD_DEFAULT
}

void gcomm::Conf::check_params(const gu::Config& conf)
{
    check_recv_buf_size(conf.get(SocketRecvBufSize));
}

size_t gcomm::Conf::check_recv_buf_size(const std::string& str)
{
    // signed type to check for negative values
    return (str == Defaults::SocketRecvBufSize ||
            check_range<long long>(SocketRecvBufSize, str,
                                   0, std::numeric_limits<long long>::max()));
}

size_t gcomm::Conf::check_send_buf_size(const std::string& str)
{
    // signed type to check for negative values
    return (str == Defaults::SocketSendBufSize ||
            check_range<long long>(SocketSendBufSize, str,
                                   0, std::numeric_limits<long long>::max()));
}
