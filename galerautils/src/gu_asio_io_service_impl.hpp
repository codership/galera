//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_asio_io_service.hpp
 *
 * Asio IO service implementation.
 */

#ifndef GU_ASIO_IO_SERVICE_HPP
#define GU_ASIO_IO_SERVICE_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio.hpp"

#include "asio/io_service.hpp"
#ifdef GALERA_HAVE_SSL
#include "asio/ssl.hpp"
#endif // GALERA_HAVE_SSL

namespace gu
{
    //
    // IO Service implementation, wraps asio types.
    //
    class AsioIoService::Impl
    {
    public:
        Impl()
            : io_service_()
#ifdef GALERA_HAVE_SSL
            , ssl_context_()
#endif // GALERA_HAVE_SSL
        { }
        asio::io_service& native() { return io_service_; }
    private:
        asio::io_service io_service_;
    public:
#ifdef GALERA_HAVE_SSL
        std::unique_ptr<asio::ssl::context> ssl_context_;
#endif // GALERA_HAVE_SSL
    };
}

#endif // GU_ASIO_IO_SERVICE_HPP
