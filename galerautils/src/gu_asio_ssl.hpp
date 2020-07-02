//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_asio_ssl.hpp
 *
 * Common helpers for SSL operations.
 */


#ifndef GU_ASIO_SSL_HPP
#define GU_ASIO_SSL_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#ifdef GALERA_HAVE_SSL

bool exclude_ssl_error(const asio::error_code& ec);
std::string extra_error_info(const asio::error_code& ec);

#else // GALERA_HAVE_SSL

static inline std::string extra_error_info(const asio::error_code&) { return ""; }

#endif // GALERA_HAVE_SSL

#endif // GU_ASIO_SSL_HPP
