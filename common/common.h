/*
 *Copyright (C) 2012-2014 Codership Oy <info@codership.com>
 */

/*! @file Stores some common definitions to be known throughout the modules */

#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#define COMMON_TCP_SCHEME "tcp"
#define COMMON_UDP_SCHEME "udp"
#define COMMON_SSL_SCHEME "ssl"
#define COMMON_DEFAULT_SCHEME COMMON_TCP_SCHEME

#define COMMON_BASE_HOST_KEY     "base_host"
#define COMMON_BASE_PORT_KEY     "base_port"
#define COMMON_BASE_PORT_DEFAULT "4567"

#define COMMON_STATE_FILE "grastate.dat"

#define COMMON_CONF_SSL_KEY       "socket.ssl_key"
#define COMMON_CONF_SSL_CERT      "socket.ssl_cert"
#define COMMON_CONF_SSL_CA        "socket.ssl_ca"
#define COMMON_CONF_SSL_PSWD_FILE "socket.ssl_password_file"

#endif // COMMON_DEFS_H
