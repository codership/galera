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
    io_service_(),
    timer_(io_service_),
    ssl_context_(io_service_, asio::ssl::context::sslv23),
    mtu_(1 << 15),
    checksum_(NetHeader::checksum_type(
                  conf.get<int>(gcomm::Conf::SocketChecksum,
                                NetHeader::CS_CRC32C)))
{
    conf.set(gcomm::Conf::SocketChecksum, checksum_);
    // use ssl if either private key or cert file is specified
    bool use_ssl(conf_.is_set(gu::conf::ssl_key)  == true ||
                 conf_.is_set(gu::conf::ssl_cert) == true);
    try
    {
        // overrides use_ssl if set explicitly
        use_ssl = conf_.get<bool>(gu::conf::use_ssl);
    }
    catch (gu::NotSet& nf) {}

    if (use_ssl == true)
    {
        conf_.set(gu::conf::use_ssl, true);
        log_info << "initializing ssl context";
        gu::ssl_prepare_context(conf_, ssl_context_);
    }
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
        return gu::shared_ptr<AsioTcpSocket>::type(new AsioTcpSocket(*this, uri));
    }
    else if (uri.get_scheme() == "udp")
    {
        return gu::shared_ptr<AsioUdpSocket>::type(new AsioUdpSocket(*this, uri));
    }
    else
    {
        gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not implemented";
    }
}

gcomm::Acceptor* gcomm::AsioProtonet::acceptor(const gu::URI& uri)
{
    return new AsioTcpAcceptor(*this, uri);
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
    timer_.expires_from_now(boost::posix_time::nanosec(p.get_nsecs()));
    timer_.async_wait(boost::bind(&AsioProtonet::handle_wait, this,
                                  asio::placeholders::error));
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


void gcomm::AsioProtonet::handle_wait(const asio::error_code& ec)
{
    gu::datetime::Date now(gu::datetime::Date::monotonic());
    const gu::datetime::Period p(handle_timers_helper(*this, poll_until_ - now));
    using std::rel_ops::operator>=;
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

