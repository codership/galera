/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_TCP_HPP
#define GCOMM_ASIO_TCP_HPP

#include "socket.hpp"

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <vector>
#include <deque>

namespace gcomm
{
    namespace asio
    {
        class Protonet;
        class TcpSocket;
        class TcpAcceptor;
    }
}

// TCP Socket implementation

class gcomm::asio::TcpSocket : 
    public gcomm::Socket,
    public boost::enable_shared_from_this<TcpSocket>
{
public:
    TcpSocket(Protonet& net, const gu::URI& uri);
    ~TcpSocket();
    void failed_handler(const boost::system::error_code& ec);
    void connect_handler(const boost::system::error_code& ec);
    void connect(const gu::URI& uri);
    void close();
    void write_handler(const boost::system::error_code& ec,
                       size_t bytes_transferred);    
    int send(const gu::net::Datagram& dg);
    size_t read_completion_condition(
        const boost::system::error_code& ec,
        const size_t bytes_transferred);
    void read_handler(const boost::system::error_code& ec,
                      const size_t bytes_transferred);
    void async_receive();
    size_t get_mtu() const;
    std::string get_local_addr() const;
    std::string get_remote_addr() const;
    State get_state() const { return state_; }
    SocketId get_id() const { return &socket_; }
private:
    friend class gcomm::asio::TcpAcceptor;

    TcpSocket(const TcpSocket&);
    void operator=(const TcpSocket&);

    Protonet&                     net_;
    boost::asio::ip::tcp::socket  socket_;
    std::deque<gu::net::Datagram> send_q_;
    std::vector<gu::byte_t>       recv_buf_;
    size_t                        recv_offset_;
    State                         state_;
};


class gcomm::asio::TcpAcceptor : public gcomm::Acceptor
{
public:
    
    TcpAcceptor(asio::Protonet& net, const gu::URI& uri);
    ~TcpAcceptor();
    void accept_handler(
        SocketPtr socket,
        const boost::system::error_code& error);
    void listen(const gu::URI& uri);
    void close();
    SocketPtr accept();
    
    State get_state() const
    {
        gu_throw_fatal << "TODO:";
        throw;
    }
    
    SocketId get_id() const { return &acceptor_; } 
    
private:
    Protonet& net_;
    boost::asio::ip::tcp::acceptor acceptor_;
    SocketPtr accepted_socket_;
};

#endif // GCOMM_ASIO_TCP_HPP
