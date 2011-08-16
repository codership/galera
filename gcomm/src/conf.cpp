/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/conf.hpp"

static std::string const Delim = ".";

// Protonet
std::string const gcomm::Conf::ProtonetBackend("protonet.backend");
std::string const gcomm::Conf::ProtonetVersion("protonet.version");

// TCP
std::string const gcomm::Conf::TcpScheme = "tcp";
std::string const gcomm::Conf::SslScheme = "ssl";
std::string const gcomm::Conf::UdpScheme = "udp";
std::string const gcomm::Conf::TcpNonBlocking =
    "socket" + Delim + "non_blocking";
std::string const gcomm::Conf::SocketUseSsl =
    "socket" + Delim + "ssl";
std::string const gcomm::Conf::SocketSslVerifyFile =
    "socket" + Delim + "ssl_ca";
std::string const gcomm::Conf::SocketSslCertificateFile =
    "socket" + Delim + "ssl_cert";
std::string const gcomm::Conf::SocketSslPrivateKeyFile =
    "socket" + Delim + "ssl_key";
std::string const gcomm::Conf::SocketSslPasswordFile =
    "socket" + Delim + "ssl_password_file";
std::string const gcomm::Conf::SocketSslCipherList =
    "socket" + Delim + "ssl_cipher";
std::string const gcomm::Conf::SocketSslCompression =
    "socket" + Delim + "ssl_compression";

// GMCast
std::string const gcomm::Conf::GMCastScheme = "gmcast";
std::string const gcomm::Conf::GMCastVersion =
    GMCastScheme + Delim + "version";
std::string const gcomm::Conf::GMCastGroup =
    GMCastScheme + Delim + "group";
std::string const gcomm::Conf::GMCastListenAddr =
    GMCastScheme + Delim + "listen_addr";
std::string const gcomm::Conf::GMCastMCastAddr =
    GMCastScheme + Delim + "mcast_addr";
std::string const gcomm::Conf::GMCastMCastPort =
    GMCastScheme + Delim + "mcast_port";
std::string const gcomm::Conf::GMCastMCastTTL =
    GMCastScheme + Delim + "mcast_ttl";
std::string const gcomm::Conf::GMCastTimeWait =
    GMCastScheme + Delim + "time_wait";
// EVS
std::string const gcomm::Conf::EvsScheme = "evs";
std::string const gcomm::Conf::EvsVersion = 
    EvsScheme + Delim + "version";
std::string const gcomm::Conf::EvsViewForgetTimeout = 
    EvsScheme + Delim + "view_forget_timeout";
std::string const gcomm::Conf::EvsInactiveTimeout = 
    EvsScheme + Delim + "inactive_timeout";
std::string const gcomm::Conf::EvsSuspectTimeout =
    EvsScheme + Delim + "suspect_timeout";
std::string const gcomm::Conf::EvsInactiveCheckPeriod = 
    EvsScheme + Delim + "inactive_check_period";
std::string const gcomm::Conf::EvsInstallTimeout =
    EvsScheme + Delim + "install_timeout";
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
std::string const gcomm::Conf::EvsUseAggregate =
    EvsScheme + Delim + "use_aggregate";
std::string const gcomm::Conf::EvsMaxInstallTimeouts =
    EvsScheme + Delim + "max_install_timeouts";
// PC
std::string const gcomm::Conf::PcScheme = "pc";
std::string const gcomm::Conf::PcVersion = PcScheme + Delim + "version";
std::string const gcomm::Conf::PcIgnoreSb = PcScheme + Delim + "ignore_sb";
std::string const gcomm::Conf::PcIgnoreQuorum = PcScheme + Delim + "ignore_quorum";
std::string const gcomm::Conf::PcChecksum = PcScheme + Delim + "checksum";
std::string const gcomm::Conf::PcLinger = PcScheme + Delim + "linger";
std::string const gcomm::Conf::PcNpvo = PcScheme + Delim + "npvo";
