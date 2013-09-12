/*
 * Copyright (C) 2010-2012 Codership Oy <info@codership.com>
 */

#include "asio_udp.hpp"
#include "asio_addr.hpp"

#include "gcomm/util.hpp"
#include "gcomm/common.hpp"

#include <boost/bind.hpp>
#include <boost/array.hpp>


static bool is_multicast(const asio::ip::udp::endpoint& ep)
{
    if (ep.address().is_v4() == true)
    {
        return ep.address().to_v4().is_multicast();
    }
    else if (ep.address().is_v6() == true)
    {
        return ep.address().to_v6().is_multicast();
    }
    gu_throw_fatal;
}

static void join_group(asio::ip::udp::socket& socket,
                       const asio::ip::udp::endpoint& ep,
                       const asio::ip::address& local_if)
{
    gcomm_assert(is_multicast(ep) == true);
    if (ep.address().is_v4() == true)
    {
        socket.set_option(asio::ip::multicast::join_group(ep.address().to_v4(),
                                                    local_if.to_v4()));
        socket.set_option(asio::ip::multicast::outbound_interface(local_if.to_v4()));
    }
    else
    {
        gu_throw_fatal << "mcast interface not implemented";
        socket.set_option(asio::ip::multicast::join_group(ep.address().to_v6()));
    }
}

static void leave_group(asio::ip::udp::socket&   socket,
                        asio::ip::udp::endpoint& ep)
{
//    gcomm_assert(is_multicast(ep) == true);
//    socket.set_option(asio::ip::multicast::leave_group(ep.address().to_v4()));
}



gcomm::AsioUdpSocket::AsioUdpSocket(AsioProtonet& net, const gu::URI& uri)
    :
    Socket(uri),
    net_(net),
    state_(S_CLOSED),
    socket_(net_.io_service_),
    target_ep_(),
    source_ep_(),
    recv_buf_((1 << 15) + NetHeader::serial_size_)
{ }


gcomm::AsioUdpSocket::~AsioUdpSocket()
{
    close();
}


void gcomm::AsioUdpSocket::connect(const gu::URI& uri)
{
    gcomm_assert(state() == S_CLOSED);
    Critical<AsioProtonet> crit(net_);
    asio::ip::udp::resolver resolver(net_.io_service_);

    asio::ip::udp::resolver::query query(unescape_addr(uri.get_host()),
                                   uri.get_port());
    asio::ip::udp::resolver::iterator conn_i(resolver.resolve(query));

    target_ep_ = conn_i->endpoint();

    socket_.open(conn_i->endpoint().protocol());
    socket_.set_option(asio::ip::udp::socket::reuse_address(true));
    socket_.set_option(asio::ip::udp::socket::linger(true, 1));
    set_fd_options(socket_);
    asio::ip::udp::socket::non_blocking_io cmd(true);
    socket_.io_control(cmd);

    const std::string if_addr(
        unescape_addr(
            uri.get_option("socket.if_addr",
                           anyaddr(conn_i->endpoint().address()))));
    asio::ip::address local_if(asio::ip::address::from_string(if_addr));

    if (is_multicast(conn_i->endpoint()) == true)
    {
        join_group(socket_, conn_i->endpoint(), local_if);
        socket_.set_option(
            asio::ip::multicast::enable_loopback(
                gu::from_string<bool>(uri.get_option("socket.if_loop", "false"))));
        socket_.set_option(
            asio::ip::multicast::hops(
                gu::from_string<int>(uri.get_option("socket.mcast_ttl", "1"))));
        socket_.bind(*conn_i);
    }
    else
    {
        socket_.bind(
            asio::ip::udp::endpoint(
                local_if,
                gu::from_string<unsigned short>(uri.get_port())));
    }

    async_receive();
    state_ = S_CONNECTED;
}

void gcomm::AsioUdpSocket::close()
{
    Critical<AsioProtonet> crit(net_);
    if (state() != S_CLOSED)
    {
        if (is_multicast(target_ep_) == true)
        {
            leave_group(socket_, target_ep_);
        }
        socket_.close();
    }
    state_ = S_CLOSED;
}

int gcomm::AsioUdpSocket::send(const Datagram& dg)
{
    Critical<AsioProtonet> crit(net_);
    boost::array<asio::const_buffer, 3> cbs;
    NetHeader hdr(dg.len(), net_.version_);

    if (net_.checksum_ != NetHeader::CS_NONE)
    {
        hdr.set_crc32(crc32(net_.checksum_, dg), net_.checksum_);
    }

    gu::byte_t buf[NetHeader::serial_size_];
    serialize(hdr, buf, sizeof(buf), 0);
    cbs[0] = asio::const_buffer(buf, sizeof(buf));
    cbs[1] = asio::const_buffer(dg.header() + dg.header_offset(),
                          dg.header_len());
    cbs[2] = asio::const_buffer(&dg.payload()[0], dg.payload().size());
    try
    {
        socket_.send_to(cbs, target_ep_);
    }
    catch (asio::system_error& err)
    {
        log_warn << "Error: " << err.what();
        return err.code().value();
    }
    return 0;
}


void gcomm::AsioUdpSocket::read_handler(const asio::error_code& ec,
                                        size_t bytes_transferred)
{
    if (ec)
    {
        //
        return;
    }

    if (bytes_transferred >= NetHeader::serial_size_)
    {
        Critical<AsioProtonet> crit(net_);
        NetHeader hdr;
        try
        {
            unserialize(&recv_buf_[0], NetHeader::serial_size_, 0, hdr);
        }
        catch (gu::Exception& e)
        {
            log_warn << "hdr unserialize failed: " << e.get_errno();
            return;
        }
        if (NetHeader::serial_size_ + hdr.len() != bytes_transferred)
        {
            log_warn << "len " << hdr.len()
                     << " does not match to bytes transferred"
                     << bytes_transferred;
        }
        else
        {
            Datagram dg(
                gu::SharedBuffer(
                    new gu::Buffer(&recv_buf_[0] + NetHeader::serial_size_,
                                   &recv_buf_[0] + NetHeader::serial_size_
                                   + hdr.len())));
            if (net_.checksum_ == true && check_cs(hdr, dg))
            {
                log_warn << "checksum failed, hdr: len=" << hdr.len()
                         << " has_crc32="  << hdr.has_crc32()
                         << " has_crc32c=" << hdr.has_crc32c()
                         << " crc32=" << hdr.crc32();
            }
            else
            {
                net_.dispatch(id(), dg, ProtoUpMeta());
            }
        }
    }
    else
    {
        log_warn << "short read of " << bytes_transferred;
    }
    async_receive();
}

void gcomm::AsioUdpSocket::async_receive()
{
    Critical<AsioProtonet> crit(net_);
    boost::array<asio::mutable_buffer, 1> mbs;
    mbs[0] = asio::mutable_buffer(&recv_buf_[0], recv_buf_.size());
    socket_.async_receive_from(mbs, source_ep_,
                               boost::bind(&AsioUdpSocket::read_handler,
                                           shared_from_this(),
                                           asio::placeholders::error,
                                           asio::placeholders::bytes_transferred));
}


size_t gcomm::AsioUdpSocket::mtu() const
{
    return (1 << 15);
}

std::string gcomm::AsioUdpSocket::local_addr() const
{
    return uri_string(UDP_SCHEME,
                      escape_addr(socket_.local_endpoint().address()),
                      gu::to_string(socket_.local_endpoint().port()));
}

std::string gcomm::AsioUdpSocket::remote_addr() const
{
    return uri_string(UDP_SCHEME,
                      escape_addr(socket_.remote_endpoint().address()),
                      gu::to_string(socket_.remote_endpoint().port()));
}
