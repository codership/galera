/*
 * Copyright (C) 2010-2017 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_UDP_HPP
#define GCOMM_ASIO_UDP_HPP

#include "socket.hpp"
#include "asio_protonet.hpp"
#include "gu_shared_ptr.hpp"
#include <vector>

#include "gu_disable_non_virtual_dtor.hpp"
#include "gu_compiler.hpp"

namespace gcomm
{
    class AsioUdpSocket;
    class AsioProtonet;
}

class gcomm::AsioUdpSocket :
    public gcomm::Socket,
    public gu::AsioDatagramSocketHandler,
    public std::enable_shared_from_this<AsioUdpSocket>
{
public:
    AsioUdpSocket(AsioProtonet& net, const gu::URI& uri);
    ~AsioUdpSocket();
    // Socket interface
    virtual void connect(const gu::URI& uri) GALERA_OVERRIDE;
    virtual void close() GALERA_OVERRIDE;
    virtual void set_option(const std::string&, const std::string&) GALERA_OVERRIDE
    { /* not implemented */ }
    virtual int send(int segment, const Datagram& dg) GALERA_OVERRIDE;
    virtual void async_receive() GALERA_OVERRIDE;
    virtual size_t mtu() const GALERA_OVERRIDE;
    virtual std::string local_addr() const GALERA_OVERRIDE;
    virtual std::string remote_addr() const GALERA_OVERRIDE;
    virtual State state() const GALERA_OVERRIDE { return state_; }
    virtual SocketId id() const GALERA_OVERRIDE { return &socket_; }
    virtual SocketStats stats() const GALERA_OVERRIDE { return SocketStats(); }
private:
    // AsioDatagramSocketHandler
    virtual void read_handler(gu::AsioDatagramSocket&, const gu::AsioErrorCode&,
                              size_t) GALERA_OVERRIDE;

    AsioProtonet&            net_;
    State                    state_;
    std::shared_ptr<gu::AsioDatagramSocket> socket_;
    std::vector<gu::byte_t>  recv_buf_;
};

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GCOMM_ASIO_UDP_HPP
