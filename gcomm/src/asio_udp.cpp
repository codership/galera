/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "asio_udp.hpp"
#include "asio.hpp"
#include "asio_addr.hpp"

#include "gcomm/util.hpp"

#include <boost/bind.hpp>
#include <boost/array.hpp>

using namespace std;
using namespace gu;
using namespace gu::net;
using namespace boost;
using namespace boost::asio;


static bool is_multicast(const ip::udp::endpoint& ep)
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
    throw;
}

static void join_group(ip::udp::socket& socket, 
                       const ip::udp::endpoint& ep,
                       const ip::address& local_if)
{
    gcomm_assert(is_multicast(ep) == true);
    if (ep.address().is_v4() == true)
    {
        socket.set_option(ip::multicast::join_group(ep.address().to_v4(), 
                                                    local_if.to_v4()));
        socket.set_option(ip::multicast::outbound_interface(local_if.to_v4()));
    }
    else
    {
        gu_throw_fatal << "mcast interface not implemented";
        socket.set_option(ip::multicast::join_group(ep.address().to_v6()));
    }
}

static void leave_group(ip::udp::socket&   socket, 
                        ip::udp::endpoint& ep)
{
//    gcomm_assert(is_multicast(ep) == true);
//    socket.set_option(ip::multicast::leave_group(ep.address().to_v4()));
}



gcomm::asio::UdpSocket::UdpSocket(Protonet& net, const URI& uri) 
    :
    Socket(uri),
    net_(net),
    state_(S_CLOSED),
    socket_(net_.io_service_),
    target_ep_(),
    source_ep_(),
    recv_buf_((1 << 15) + 4)
{ }


gcomm::asio::UdpSocket::~UdpSocket()
{
    close();
}


void gcomm::asio::UdpSocket::connect(const URI& uri)
{
    gcomm_assert(get_state() == S_CLOSED);
    Critical<Protonet> crit(net_);
    ip::udp::resolver resolver(net_.io_service_);
    
    ip::udp::resolver::query query(unescape_addr(uri.get_host()), 
                                   uri.get_port());
    ip::udp::resolver::iterator conn_i(resolver.resolve(query));       
    
    target_ep_ = conn_i->endpoint();

    socket_.open(conn_i->endpoint().protocol());
    socket_.set_option(ip::udp::socket::reuse_address(true));    
    ip::udp::socket::non_blocking_io cmd(true);
    socket_.io_control(cmd);
    
    const string if_addr(
        unescape_addr(
            uri.get_option("socket.if_addr", 
                           anyaddr(conn_i->endpoint().address()))));
    ip::address local_if(ip::address::from_string(if_addr));
    
    if (is_multicast(conn_i->endpoint()) == true)
    {
        join_group(socket_, conn_i->endpoint(), local_if);
        socket_.set_option(
            ip::multicast::enable_loopback(
                from_string<bool>(uri.get_option("socket.if_loop", "false"))));
        socket_.set_option(
            ip::multicast::hops(
                from_string<int>(uri.get_option("socket.mcast_ttl", "1"))));
        socket_.bind(*conn_i);
    }
    else
    {
        socket_.bind(
            ip::udp::endpoint(local_if, 
                              from_string<unsigned short>(uri.get_port())));
    }
    
    async_receive();
    state_ = S_CONNECTED;
}

void gcomm::asio::UdpSocket::close()
{
    Critical<Protonet> crit(net_);
    if (get_state() != S_CLOSED)
    {
        if (is_multicast(target_ep_) == true)
        {
            leave_group(socket_, target_ep_);
        }
        socket_.close();
    }
    state_ = S_CLOSED;
}

int gcomm::asio::UdpSocket::send(const Datagram& dg)
{
    Critical<Protonet> crit(net_);
    array<const_buffer, 3> cbs;
    uint32_t len(dg.get_len());
    byte_t buf[4];
    gcomm::serialize(len, buf, sizeof(buf), 0);
    cbs[0] = const_buffer(buf, sizeof(buf));
    cbs[1] = const_buffer(dg.get_header() + dg.get_header_offset(),
                          dg.get_header_len());
    cbs[2] = const_buffer(&dg.get_payload()[0], dg.get_payload().size());
    socket_.send_to(cbs, target_ep_);
    return 0;
}


void gcomm::asio::UdpSocket::read_handler(const system::error_code& ec, 
                                          size_t bytes_transferred)
{
    if (ec != 0)
    {
        // 
        return;
    }
    
    if (bytes_transferred >= 4)
    {
        Critical<Protonet> crit(net_);         
        uint32_t len;
        unserialize(&recv_buf_[0], 4, 0, &len);
        
        if (4 + len != bytes_transferred)
        {
            log_warn << "len " << len 
                     << " does not match to bytes transferred" 
                     << bytes_transferred;
        }
        else
        {
            Datagram dg(SharedBuffer(new Buffer(&recv_buf_[0] + 4,
                                                &recv_buf_[0] + 4 + len),
                                     BufferDeleter(),
                                     shared_buffer_allocator));
            net_.dispatch(get_id(), dg, ProtoUpMeta());
        }
    }
    else
    {
        
        log_warn << "short read of " << bytes_transferred;
    }
    async_receive();
}

void gcomm::asio::UdpSocket::async_receive()
{
    Critical<Protonet> crit(net_);
    array<mutable_buffer, 1> mbs;
    mbs[0] = mutable_buffer(&recv_buf_[0], recv_buf_.size());
    socket_.async_receive_from(mbs, source_ep_,
                               boost::bind(&UdpSocket::read_handler,
                                           shared_from_this(),
                                           placeholders::error,
                                           placeholders::bytes_transferred));
}


size_t gcomm::asio::UdpSocket::get_mtu() const
{
    return (1 << 15);
}

string gcomm::asio::UdpSocket::get_local_addr() const
{
    return "udp://" 
        + escape_addr(socket_.local_endpoint().address())
        + ":"
        + to_string(socket_.local_endpoint().port());
}

string gcomm::asio::UdpSocket::get_remote_addr() const
{
    return "udp://" 
        + escape_addr(socket_.remote_endpoint().address())
        + ":"
        + to_string(socket_.remote_endpoint().port());
}

