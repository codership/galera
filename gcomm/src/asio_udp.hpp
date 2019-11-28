/*
 * Copyright (C) 2010-2017 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_UDP_HPP
#define GCOMM_ASIO_UDP_HPP

#include "socket.hpp"
#include "asio_protonet.hpp"
#include "gu_shared_ptr.hpp"
#include <vector>

//
// Boost enable_shared_from_this<> does not have virtual destructor,
// therefore need to ignore -Weffc++ and -Wnon-virtual-dtor
//
#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic push
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
# pragma GCC diagnostic ignored "-Weffc++"
# pragma GCC diagnostic ignored "-Wnon-virtual-dtor"
#endif

namespace gcomm
{
    class AsioUdpSocket;
    class AsioProtonet;
}

class gcomm::AsioUdpSocket :
    public gcomm::Socket,
    public gu::enable_shared_from_this<AsioUdpSocket>::type
{
public:
    AsioUdpSocket(AsioProtonet& net, const gu::URI& uri);
    ~AsioUdpSocket();
    void connect(const gu::URI& uri);
    void close();
    void set_option(const std::string&, const std::string&) { /* not implemented */ }
    int send(int segment, const Datagram& dg);
    void read_handler(const asio::error_code&, size_t);
    void async_receive();
    size_t mtu() const;
    std::string local_addr() const;
    std::string remote_addr() const;
    State state() const { return state_; }
    SocketId id() const { return &socket_; }
    SocketStats stats() const { return SocketStats(); }
private:
    AsioProtonet&            net_;
    State                    state_;
    asio::ip::udp::socket    socket_;
    asio::ip::udp::endpoint  target_ep_;
    asio::ip::udp::endpoint  source_ep_;
    std::vector<gu::byte_t>  recv_buf_;
};

#if defined(__GNUG__)
# if (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#  pragma GCC diagnostic pop
# endif // (__GNUC__ == 4 && __GNUC_MINOR__ >= 6) || (__GNUC__ > 4)
#endif

#endif // GCOMM_ASIO_UDP_HPP
