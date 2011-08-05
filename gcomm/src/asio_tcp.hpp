/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_TCP_HPP
#define GCOMM_ASIO_TCP_HPP

#include "socket.hpp"
#include "asio_protonet.hpp"

#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <vector>
#include <deque>

namespace gcomm
{
    class AsioTcpSocket;
    class AsioTcpAcceptor;
}

// TCP Socket implementation

class gcomm::AsioTcpSocket :
    public gcomm::Socket,
    public boost::enable_shared_from_this<AsioTcpSocket>
{
public:
    AsioTcpSocket(AsioProtonet& net, const gu::URI& uri);
    ~AsioTcpSocket();
    void failed_handler(const asio::error_code& ec, const std::string& func, int line);
#ifdef HAVE_ASIO_SSL_HPP
    void handshake_handler(const asio::error_code& ec);
#endif // HAVE_ASIO_SSL_HPP
    void connect_handler(const asio::error_code& ec);
    void connect(const gu::URI& uri);
    void close();
    void write_handler(const asio::error_code& ec,
                       size_t bytes_transferred);
    int send(const gu::Datagram& dg);
    size_t read_completion_condition(
        const asio::error_code& ec,
        const size_t bytes_transferred);
    void read_handler(const asio::error_code& ec,
                      const size_t bytes_transferred);
    void async_receive();
    size_t get_mtu() const;
    std::string get_local_addr() const;
    std::string get_remote_addr() const;
    State get_state() const { return state_; }
    SocketId get_id() const { return &socket_; }
private:
    friend class gcomm::AsioTcpAcceptor;

    AsioTcpSocket(const AsioTcpSocket&);
    void operator=(const AsioTcpSocket&);

    void read_one(boost::array<asio::mutable_buffer, 1>& mbs);
    void write_one(const boost::array<asio::const_buffer, 2>& cbs);
    void close_socket();

    AsioProtonet&                             net_;
    asio::ip::tcp::socket                     socket_;
#ifdef HAVE_ASIO_SSL_HPP
    asio::ssl::stream<asio::ip::tcp::socket>* ssl_socket_;
#endif // HAVE_ASIO_SSL_HPP
    std::deque<gu::Datagram>                  send_q_;
    std::vector<gu::byte_t>                   recv_buf_;
    size_t                                    recv_offset_;
    State                                     state_;
};


class gcomm::AsioTcpAcceptor : public gcomm::Acceptor
{
public:

    AsioTcpAcceptor(AsioProtonet& net, const gu::URI& uri);
    ~AsioTcpAcceptor();
    void accept_handler(
        SocketPtr socket,
        const asio::error_code& error);
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
    AsioProtonet& net_;
    asio::ip::tcp::acceptor acceptor_;
    SocketPtr accepted_socket_;
};

#endif // GCOMM_ASIO_TCP_HPP
