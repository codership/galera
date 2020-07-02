//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_asio_datagram.hpp
 *
 * Datagram socket implementation.
 */

#ifndef GU_ASIO_DATAGRAM_HPP
#define GU_ASIO_DATAGRAM_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio.hpp"

#include "asio/ip/address.hpp"
#include "asio/ip/udp.hpp"

#include "gu_disable_non_virtual_dtor.hpp"
#include "gu_compiler.hpp"

namespace gu
{
    //
    // UDP/Datagram wrapper
    //
    class AsioUdpSocket : public AsioDatagramSocket
                        , public std::enable_shared_from_this<AsioUdpSocket>
    {
    public:
        AsioUdpSocket(gu::AsioIoService& io_service);

        ~AsioUdpSocket();

        asio::ip::udp::resolver::iterator resolve_and_open(const gu::URI& uri);

        virtual void open(const gu::URI& uri) GALERA_OVERRIDE;

        virtual void close() GALERA_OVERRIDE;

        virtual void connect(const gu::URI& uri) GALERA_OVERRIDE;

        virtual void write(const std::array<AsioConstBuffer, 2>& buffers)
            GALERA_OVERRIDE;

        virtual void send_to(const std::array<AsioConstBuffer, 2>& buffers,
                             const AsioIpAddress& target_host,
                             unsigned short target_port) GALERA_OVERRIDE;

        virtual void async_read(
            const AsioMutableBuffer& buffer,
            const std::shared_ptr<AsioDatagramSocketHandler>& handler)
            GALERA_OVERRIDE;

        virtual std::string local_addr() const GALERA_OVERRIDE;

        // Async handlers
        void read_handler(
            const std::shared_ptr<AsioDatagramSocketHandler>& handler,
            const asio::error_code& ec,
            size_t bytes_transferred);

    private:
        AsioIoService& io_service_;
        asio::ip::udp::socket socket_;
        asio::ip::udp::endpoint local_endpoint_;
        asio::ip::address local_if_;
    };
}

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GU_ASIO_DATAGRAM_HPP
