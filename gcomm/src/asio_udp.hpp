/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_UDP_HPP
#define GCOMM_ASIO_UDP_HPP

#include "socket.hpp"
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <vector>

namespace gcomm
{
    namespace asio
    {
        class UdpSocket;
        class Protonet;
    }
}

class gcomm::asio::UdpSocket : 
    public gcomm::Socket,
    public boost::enable_shared_from_this<UdpSocket>
{
public:
    UdpSocket(Protonet& net, const gu::URI& uri);
    ~UdpSocket();
    void connect(const gu::URI& uri);
    void close();
    int send(const gu::Datagram& dg);
    void read_handler(const boost::system::error_code&, size_t);
    void async_receive();
    size_t get_mtu() const;
    std::string get_local_addr() const;
    std::string get_remote_addr() const;
    State get_state() const { return state_; }
    SocketId get_id() const { return &socket_; }

private:
    Protonet&          net_;
    State              state_;
    boost::asio::ip::udp::socket    socket_;
    boost::asio::ip::udp::endpoint  target_ep_;
    boost::asio::ip::udp::endpoint  source_ep_;
    std::vector<gu::byte_t> recv_buf_;
};

#endif // GCOMM_ASIO_UDP_HPP
