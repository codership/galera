/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */


#include "asio_tcp.hpp"
#include "asio_udp.hpp"
#include "asio_addr.hpp"
#include "asio_protonet.hpp"

#include "socket.hpp"

#include "gcomm/util.hpp"

#include "gu_logger.hpp"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;


gcomm::AsioProtonet::AsioProtonet(int version)
    :
    gcomm::Protonet("asio"),
    mutex_(),
    poll_until_(Date::max()),
    io_service_(),
    timer_(io_service_),
    version_(version),
    mtu_(1 << 15),
    checksum_(true)
{

}

gcomm::AsioProtonet::~AsioProtonet()
{

}

void gcomm::AsioProtonet::enter()
{
    mutex_.lock();
}



void gcomm::AsioProtonet::leave()
{
    mutex_.unlock();
}

gcomm::SocketPtr gcomm::AsioProtonet::socket(const URI& uri)
{
    if (uri.get_scheme() == "tcp")
    {
        return boost::shared_ptr<AsioTcpSocket>(new AsioTcpSocket(*this, uri));

    }
    else if (uri.get_scheme() == "udp")
    {
        return boost::shared_ptr<AsioUdpSocket>(new AsioUdpSocket(*this, uri));
    }
    else
    {
        gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not implemented";
        throw;
    }
}

gcomm::Acceptor* gcomm::AsioProtonet::acceptor(const URI& uri)
{
    return new AsioTcpAcceptor(*this, uri);
}



Period handle_timers_helper(gcomm::Protonet& pnet, const Period& period)
{
    const Date now(Date::now());
    const Date stop(now + period);

    const Date next_time(pnet.handle_timers());
    const Period sleep_p(min(stop - now, next_time - now));
    return (sleep_p < 0 ? 0 : sleep_p);
}


void gcomm::AsioProtonet::event_loop(const Period& period)
{
    io_service_.reset();
    poll_until_ = Date::now() + period;

    const Period p(handle_timers_helper(*this, period));
    timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
    timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                  asio::placeholders::error));
    io_service_.run();
}


void gcomm::AsioProtonet::dispatch(const SocketId& id,
                                   const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    for (deque<Protostack*>::iterator i = protos_.begin();
         i != protos_.end(); ++i)
    {
        (*i)->dispatch(id, dg, um);
    }
}


void gcomm::AsioProtonet::interrupt()
{
    io_service_.stop();
}


void gcomm::AsioProtonet::handle_wait(const asio::error_code& ec)
{
    Date now(Date::now());
    const Period p(handle_timers_helper(*this, poll_until_ - now));
    if (ec == asio::error_code() && poll_until_ >= now)
    {
        timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
        timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                      asio::placeholders::error));
    }
    else
    {
        io_service_.stop();
    }
}
