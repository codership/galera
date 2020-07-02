//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#define GU_ASIO_IMPL

#include "gu_asio_datagram.hpp"
#include "gu_asio_error_category.hpp"
#include "gu_asio_io_service_impl.hpp"
#include "gu_asio_ip_address_impl.hpp"
#include "gu_asio_utils.hpp"
#include "gu_asio_socket_util.hpp"

#ifndef ASIO_HAS_BOOST_BIND
#define ASIO_HAS_BOOST_BIND
#endif // ASIO_HAS_BOOST_BIND
#include "asio/ip/multicast.hpp"
#include "asio/placeholders.hpp"

#include <boost/bind.hpp>

static asio::ip::udp::resolver::iterator resolve_udp(
    asio::io_service& io_service,
    const gu::URI& uri)
{
    asio::ip::udp::resolver resolver(io_service);
    asio::ip::udp::resolver::query query(gu::unescape_addr(uri.get_host()),
                                         uri.get_port());
    return resolver.resolve(query);
}

static bool is_multicast(const asio::ip::udp::endpoint& ep)
{
    if (ep.address().is_v4() == true)
    {
        return ep.address().to_v4().is_multicast();
    }
    else if (ep.address().is_v6() == true)
    {
        return ep.address().to_v6().is_multicast();
    }
    gu_throw_fatal;
}

static void join_group(asio::ip::udp::socket& socket,
                       const asio::ip::udp::endpoint& ep,
                       const asio::ip::address& local_if)
{
    assert(is_multicast(ep) == true);
    if (ep.address().is_v4() == true)
    {
        socket.set_option(asio::ip::multicast::join_group(
                              ep.address().to_v4(), local_if.to_v4()));
        socket.set_option(asio::ip::multicast::outbound_interface(
                              local_if.to_v4()));
    }
    else
    {
        gu_throw_fatal << "mcast interface not implemented for IPv6";
        socket.set_option(asio::ip::multicast::join_group(ep.address().to_v6()));
    }
}

static void leave_group(asio::ip::udp::socket&   socket,
                        const asio::ip::udp::endpoint& ep,
                        const asio::ip::address& local_if)
{
    // @todo This was commented out in the original code.
    assert(is_multicast(ep) == true);
    try
    {
        socket.set_option(asio::ip::multicast::leave_group(
                              ep.address().to_v4(), local_if.to_v4()));
    }
    catch (const asio::system_error& e)
    {
        // @todo Exception is caught here if socket.if_addr option
        //       is given when connecting to multicast group.
        log_warn << "Caught error while leaving multicast group: "
                 << e.what()
                 << ": " << ep.address();
        assert(0);
    }
}

gu::AsioUdpSocket::AsioUdpSocket(gu::AsioIoService& io_service)
    : io_service_(io_service)
    , socket_(io_service.impl().native())
    , local_endpoint_()
    , local_if_()
{ }

gu::AsioUdpSocket::~AsioUdpSocket() { close(); }

asio::ip::udp::resolver::iterator
gu::AsioUdpSocket::resolve_and_open(const gu::URI& uri)
{
    try
    {
        auto resolve_result(resolve_udp(io_service_.impl().native(), uri));
        socket_.open(resolve_result->endpoint().protocol());
        set_fd_options(socket_);
        return resolve_result;
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "error opening datagram socket" << uri;
    }
}

void gu::AsioUdpSocket::open(const gu::URI& uri)
{
    try
    {
        resolve_and_open(uri);
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "error opening datagram socket" << uri;
    }
}

void gu::AsioUdpSocket::close()
{
    if (socket_.is_open())
    {
        if (is_multicast(socket_.local_endpoint()))
        {
            leave_group(socket_, socket_.local_endpoint(), local_if_);
        }
        socket_.close();
    }
}

void gu::AsioUdpSocket::connect(const gu::URI& uri)
{
    try
    {
        asio::ip::udp::resolver::iterator resolve_result;
        if (not socket_.is_open())
        {
            resolve_result = resolve_and_open(uri);
        }
        else
        {
            resolve_result = resolve_udp(io_service_.impl().native(), uri);
        }

        socket_.set_option(asio::ip::udp::socket::reuse_address(true));
        socket_.set_option(asio::ip::udp::socket::linger(true, 1));
#if ASIO_VERSION < 101600
        asio::ip::udp::socket::non_blocking_io non_blocking(true);
        socket_.io_control(non_blocking);
#else
        socket_.non_blocking(true);
#endif

        local_if_ =
            ::make_address(
                uri.get_option("socket.if_addr",
                               ::any_addr(
                                   resolve_result->endpoint().address())));

        if (is_multicast(resolve_result->endpoint()))
        {
            join_group(socket_, resolve_result->endpoint(), local_if_);
            socket_.set_option(
                asio::ip::multicast::enable_loopback(
                    gu::from_string<bool>(uri.get_option("socket.if_loop", "false"))));
            socket_.set_option(
                asio::ip::multicast::hops(
                    gu::from_string<int>(uri.get_option("socket.mcast_ttl", "1"))));
            socket_.bind(*resolve_result);
        }
        else
        {
            socket_.bind(
                asio::ip::udp::endpoint(
                    local_if_,
                    gu::from_string<unsigned short>(uri.get_port())));
        }
        local_endpoint_ = socket_.local_endpoint();
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to connect UDP socket: " << e.what();
    }
}

void gu::AsioUdpSocket::write(
    const std::array<AsioConstBuffer, 2>& buffers)
    try
{
    std::array<asio::const_buffer, 2> asio_bufs;
    asio_bufs[0] = asio::const_buffer(buffers[0].data(),buffers[0].size());
    asio_bufs[1] = asio::const_buffer(buffers[1].data(),buffers[1].size());
    socket_.send_to(asio_bufs, local_endpoint_);
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value())
        << "Failed to write UDP socket: " << e.what();
}

void gu::AsioUdpSocket::send_to(
    const std::array<AsioConstBuffer, 2>& buffers,
    const AsioIpAddress& target_host,
    unsigned short target_port)
{
    std::array<asio::const_buffer, 2> asio_bufs;
    asio_bufs[0] = asio::const_buffer(buffers[0].data(), buffers[0].size());
    asio_bufs[1] = asio::const_buffer(buffers[1].data(), buffers[1].size());
    asio::ip::udp::endpoint target_endpoint(
        target_host.impl().native(), target_port);
    try
    {
        socket_.send_to(asio_bufs, target_endpoint);
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "Failed to send datagram to "
            << target_endpoint << ": " << e.what();
    }
}

void gu::AsioUdpSocket::async_read(
    const AsioMutableBuffer& buffer,
    const std::shared_ptr<AsioDatagramSocketHandler>& handler)
{
    socket_.async_receive(asio::buffer(buffer.data(), buffer.size()),
                          boost::bind(&AsioUdpSocket::read_handler,
                                      shared_from_this(),
                                      handler,
                                      asio::placeholders::error,
                                      asio::placeholders::bytes_transferred));
}

std::string gu::AsioUdpSocket::local_addr() const
{
    return uri_string(gu::scheme::udp,
                      ::escape_addr(socket_.local_endpoint().address()),
                      gu::to_string(socket_.local_endpoint().port()));
}


// Async handlers
void gu::AsioUdpSocket::read_handler(
    const std::shared_ptr<AsioDatagramSocketHandler>& handler,
    const asio::error_code& ec,
    size_t bytes_transferred)
{
    handler->read_handler(*this,
                          AsioErrorCode(ec.value(), ec.category()),
                          bytes_transferred);
}
