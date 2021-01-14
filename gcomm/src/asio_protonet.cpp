/*
 * Copyright (C) 2010-2019 Codership Oy <info@codership.com>
 */


#include "asio_tcp.hpp"
#include "asio_udp.hpp"
#include "asio_protonet.hpp"

#include "socket.hpp"

#include "gcomm/util.hpp"
#include "gcomm/conf.hpp"

#include "gu_logger.hpp"
#include "gu_shared_ptr.hpp"

#include <boost/bind.hpp>

#include <fstream>


gcomm::AsioProtonet::AsioProtonet(gu::Config& conf, int version)
    :
    gcomm::Protonet(conf, "asio", version),
    mutex_(),
    poll_until_(gu::datetime::Date::max()),
    io_service_(conf),
    timer_handler_(std::make_shared<TimerHandler>(*this)),
    timer_(io_service_),
    mtu_(1 << 15),
    checksum_(NetHeader::checksum_type(
                  conf.get<int>(gcomm::Conf::SocketChecksum,
                                NetHeader::CS_CRC32C)))
{
    conf.set(gcomm::Conf::SocketChecksum, checksum_);
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

gcomm::SocketPtr gcomm::AsioProtonet::socket(const gu::URI& uri)
{
    if (uri.get_scheme() == "tcp" || uri.get_scheme() == "ssl")
    {
        return std::make_shared<AsioTcpSocket>(*this, uri);
    }
    else if (uri.get_scheme() == "udp")
    {
        return std::make_shared<AsioUdpSocket>(*this, uri);
    }
    else
    {
        gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not implemented";
    }
}

std::shared_ptr<gcomm::Acceptor> gcomm::AsioProtonet::acceptor(
    const gu::URI& uri)
{
    return std::make_shared<AsioTcpAcceptor>(*this, uri);
}



gu::datetime::Period handle_timers_helper(gcomm::Protonet&            pnet,
                                          const gu::datetime::Period& period)
{
    const gu::datetime::Date now(gu::datetime::Date::monotonic());
    const gu::datetime::Date stop(now + period);

    const gu::datetime::Date next_time(pnet.handle_timers());
    const gu::datetime::Period sleep_p(std::min(stop - now, next_time - now));
    return (sleep_p < 0 ? 0 : sleep_p);
}


void gcomm::AsioProtonet::event_loop(const gu::datetime::Period& period)
{
    io_service_.reset();
    poll_until_ = gu::datetime::Date::monotonic() + period;

    const gu::datetime::Period p(handle_timers_helper(*this, period));
    // Use microsecond precision to avoid
    // "the resulting duration is not exactly representable"
    // static assertion with GCC 4.4.
    timer_.expires_from_now(std::chrono::microseconds(p.get_nsecs()/1000));
    timer_.async_wait(timer_handler_);
    io_service_.run();
}


void gcomm::AsioProtonet::dispatch(const SocketId& id,
                                   const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    for (std::deque<Protostack*>::iterator i = protos_.begin();
         i != protos_.end(); ++i)
    {
        (*i)->dispatch(id, dg, um);
    }
}


void gcomm::AsioProtonet::interrupt()
{
    io_service_.stop();
}


void gcomm::AsioProtonet::handle_wait(const gu::AsioErrorCode& ec)
{
    gu::datetime::Date now(gu::datetime::Date::monotonic());
    const gu::datetime::Period p(handle_timers_helper(*this, poll_until_ - now));
    using std::rel_ops::operator>=;
    if (not ec && poll_until_ >= now)
    {
        // Use microsecond precision to avoid
        // "the resulting duration is not exactly representable"
        // static assertion with GCC 4.4.
        timer_.expires_from_now(std::chrono::microseconds(p.get_nsecs()/1000));
        timer_.async_wait(timer_handler_);
    }
    else
    {
        io_service_.stop();
    }
}

