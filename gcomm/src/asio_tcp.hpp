/*
 * Copyright (C) 2010-2019 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_TCP_HPP
#define GCOMM_ASIO_TCP_HPP

#include "socket.hpp"
#include "asio_protonet.hpp"
#include "fair_send_queue.hpp"

#include "gu_array.hpp"
#include "gu_shared_ptr.hpp"

#include <vector>
#include <deque>

#include "gu_disable_non_virtual_dtor.hpp"
#include "gu_compiler.hpp"

/**
 * Configuration value denoting automatic buffer size adjustment for
 * socket.recv_buf_size and socket.send_buf_size.
 */
#define GCOMM_ASIO_AUTO_BUF_SIZE "auto"

namespace gcomm
{
    class AsioTcpSocket;
    class AsioTcpAcceptor;
    class AsioPostForSendHandler;
}

// TCP Socket implementation

class gcomm::AsioTcpSocket :
    public gcomm::Socket,
    public gu::AsioSocketHandler,
    public std::enable_shared_from_this<AsioTcpSocket>
{
public:
    AsioTcpSocket(AsioProtonet& net, const gu::URI& uri);
    AsioTcpSocket(AsioProtonet& net, const gu::URI& uri,
                  const std::shared_ptr<gu::AsioSocket>&);
    ~AsioTcpSocket();
    void failed_handler(const gu::AsioErrorCode& ec,
                        const std::string& func, int line);
    // Socket interface
    virtual void connect(const gu::URI& uri) GALERA_OVERRIDE;
    virtual void close() GALERA_OVERRIDE;
    virtual void set_option(const std::string& key, const std::string& val) GALERA_OVERRIDE;
    virtual int send(int segment, const Datagram& dg) GALERA_OVERRIDE;
    virtual void async_receive() GALERA_OVERRIDE;
    virtual size_t mtu() const GALERA_OVERRIDE;
    virtual std::string local_addr() const GALERA_OVERRIDE;
    virtual std::string remote_addr() const GALERA_OVERRIDE;
    virtual State state() const GALERA_OVERRIDE { return state_; }
    virtual SocketId id() const GALERA_OVERRIDE { return &socket_; }
    virtual SocketStats stats() const GALERA_OVERRIDE;
private:
    // AsioSocketHandler interface
    virtual void connect_handler(gu::AsioSocket&, const gu::AsioErrorCode&) GALERA_OVERRIDE;
    virtual void write_handler(gu::AsioSocket&, const gu::AsioErrorCode&, size_t) GALERA_OVERRIDE;
    virtual size_t read_completion_condition(gu::AsioSocket&, const gu::AsioErrorCode&,
                                             size_t) GALERA_OVERRIDE;
    virtual void read_handler(gu::AsioSocket&, const gu::AsioErrorCode&, size_t) GALERA_OVERRIDE;

    // 
    friend class gcomm::AsioTcpAcceptor;
    friend class gcomm::AsioPostForSendHandler;

    
    AsioTcpSocket(const AsioTcpSocket&);
    void operator=(const AsioTcpSocket&);

    void set_buf_sizes();
    void init_tstamps()
    {
        gu::datetime::Date now(gu::datetime::Date::monotonic());
        last_queued_tstamp_ = last_delivered_tstamp_ = now;
    }
    void cancel_deferred_close_timer();

    AsioProtonet&                             net_;
    std::shared_ptr<gu::AsioSocket>           socket_;
    // Limit the number of queued bytes. This workaround to avoid queue
    // pile up due to frequent retransmissions by the upper layers (evs).
    // It is a responsibility of upper layers (evs) to request resending
    // of dropped messaes. Upper limit (32MB) is enough to hold 1024
    // datagrams with default gcomm MTU 32kB.
    static const size_t                       max_send_q_bytes = (1 << 25);
    gcomm::FairSendQueue                      send_q_;
    gu::datetime::Date                        last_queued_tstamp_;
    std::vector<gu::byte_t>                   recv_buf_;
    size_t                                    recv_offset_;
    gu::datetime::Date                        last_delivered_tstamp_;
    State                                     state_;

    class DeferredCloseTimer;
    std::weak_ptr<DeferredCloseTimer>       deferred_close_timer_;
};

class gcomm::AsioTcpAcceptor
    : public gcomm::Acceptor
    , public gu::AsioAcceptorHandler
    , public std::enable_shared_from_this<AsioTcpAcceptor>
{
public:

    AsioTcpAcceptor(AsioProtonet& net, const gu::URI& uri);
    ~AsioTcpAcceptor();
    void set_buf_sizes();
    void listen(const gu::URI& uri);
    std::string listen_addr() const;
    void close();
    SocketPtr accept();

    State state() const
    {
        gu_throw_fatal << "TODO:";
    }

    SocketId id() const { return &acceptor_; }

private:

    void accept_handler(
        gu::AsioAcceptor&,
        const std::shared_ptr<gu::AsioSocket>&,
        const gu::AsioErrorCode& error);

    AsioProtonet& net_;
    std::shared_ptr<gu::AsioAcceptor> acceptor_;
    SocketPtr accepted_socket_;
};

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GCOMM_ASIO_TCP_HPP
