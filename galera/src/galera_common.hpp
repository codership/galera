/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 */

/*!
 * @file common.hpp
 *
 * @brief Imports definitions from the global common.h
 */

#ifndef GALERA_COMMON_HPP
#define GALERA_COMMON_HPP

#if defined(HAVE_COMMON_H)
#include <common.h>
#endif

#include <string>

namespace galera
{
#if defined(HAVE_COMMON_H)
    static std::string const TCP_SCHEME(COMMON_TCP_SCHEME);
    static std::string const UDP_SCHEME(COMMON_UDP_SCHEME);
    static std::string const SSL_SCHEME(COMMON_SSL_SCHEME);

    static std::string const BASE_PORT_KEY(COMMON_BASE_PORT_KEY);
    static std::string const BASE_PORT_DEFAULT(COMMON_BASE_PORT_DEFAULT);

    static std::string const BASE_HOST_KEY(COMMON_BASE_HOST_KEY);

    static std::string const GALERA_STATE_FILE(COMMON_STATE_FILE);
#else
    static std::string const TCP_SCHEME("tcp");
    static std::string const UDP_SCHEME("udp");
    static std::string const SSL_SCHEME("ssl");

    static std::string const BASE_PORT_KEY("base_port");
    static std::string const BASE_PORT_DEFAULT("4567");

    static std::string const BASE_HOST_KEY("base_host");

    static std::string const GALERA_STATE_FILE("grastate.dat");
#endif
}

#endif /* GALERA_COMMON_HPP */
