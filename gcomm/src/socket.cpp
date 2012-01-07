//
// Copyright (C) 2012 Codership Oy <info@codership.com>
//

#include "socket.hpp"

static const std::string SocketOptPrefix = "socket.";

const std::string gcomm::Socket::OptNonBlocking = SocketOptPrefix + "non_blocking";
const std::string gcomm::Socket::OptIfAddr      = SocketOptPrefix + "if_addr";
const std::string gcomm::Socket::OptIfLoop      = SocketOptPrefix + "if_loop";
const std::string gcomm::Socket::OptCRC32       = SocketOptPrefix + "crc32";
const std::string gcomm::Socket::OptMcastTTL    = SocketOptPrefix + "mcast_ttl";
