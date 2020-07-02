/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_PROTONET_HPP
#define GCOMM_ASIO_PROTONET_HPP

#include "gcomm/protonet.hpp"
#include "socket.hpp"

#include "gu_monitor.hpp"
#include "gu_asio.hpp"

#include <vector>
#include <deque>
#include <list>

#include "gu_disable_non_virtual_dtor.hpp"

namespace gcomm
{
    class AsioProtonet;
}

class gcomm::AsioProtonet : public gcomm::Protonet
{
public:

    AsioProtonet(gu::Config& conf, int version = 0);
    ~AsioProtonet();
    void event_loop(const gu::datetime::Period& p);
    void dispatch(const SocketId&,
                  const Datagram&,
                  const ProtoUpMeta&);
    void interrupt();
    SocketPtr socket(const gu::URI&);
    std::shared_ptr<gcomm::Acceptor> acceptor(const gu::URI&);
    void enter();
    void leave();
    size_t mtu() const { return mtu_; }

    std::string get_ssl_password() const;

private:

    class TimerHandler : public gu::AsioSteadyTimerHandler
                       , public std::enable_shared_from_this<TimerHandler>
    {
    public:
        TimerHandler(AsioProtonet& pnet)
            : pnet_(pnet)
        { }
        void handle_wait(const gu::AsioErrorCode& ec)
        {
            return pnet_.handle_wait(ec);
        }
    private:
        AsioProtonet& pnet_;
    };

    friend class AsioTcpSocket;
    friend class AsioTcpAcceptor;
    friend class AsioUdpSocket;
    AsioProtonet(const AsioProtonet&);

    void handle_wait(const gu::AsioErrorCode& ec);

    gu::RecursiveMutex          mutex_;
    gu::datetime::Date          poll_until_;
    gu::AsioIoService           io_service_;
    std::shared_ptr<TimerHandler> timer_handler_;
    gu::AsioSteadyTimer         timer_;
    size_t                      mtu_;

    NetHeader::checksum_t       checksum_;
};

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GCOMM_ASIO_PROTONET_HPP
