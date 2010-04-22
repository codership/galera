/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_HPP
#define GCOMM_ASIO_HPP

#include "gcomm/protonet.hpp"
#include "socket.hpp"

#include "gu_monitor.hpp"

#include <boost/asio.hpp>

#include <vector>
#include <deque>
#include <list>

namespace gcomm
{
    namespace asio
    {
        class Protonet;
    }
}




class gcomm::asio::Protonet : public gcomm::Protonet
{
public:
    Protonet();
    ~Protonet();
    void event_loop(const gu::datetime::Period& p);  
    void dispatch(const SocketId&,
                  const gu::net::Datagram&, 
                  const ProtoUpMeta&);
    void interrupt();
    SocketPtr socket(const gu::URI&);
    gcomm::Acceptor* acceptor(const gu::URI&);
    void enter();
    void leave();
    size_t get_mtu() const { return mtu_; }
private:
    friend class TcpSocket;
    friend class TcpAcceptor;
    friend class UdpSocket;
    Protonet(const Protonet&);

    void handle_wait(const boost::system::error_code& ec);

    gu::datetime::Date          poll_until_;
    boost::asio::io_service     io_service_;
    boost::asio::deadline_timer timer_;
    gu::RecursiveMutex          mutex_;
    size_t                      mtu_;
};

#endif // GCOMM_ASIO_HPP

