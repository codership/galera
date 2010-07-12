/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "asio_tcp.hpp"
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

gcomm::asio::TcpSocket::TcpSocket(Protonet& net, const URI& uri)
    :
    Socket      (uri),
    net_        (net),
    socket_     (net.io_service_),
    recv_buf_   (net_.get_mtu() + 4),
    recv_offset_(0),
    state_      (S_CLOSED)
{ }

gcomm::asio::TcpSocket::~TcpSocket()
{
    close();
}

void gcomm::asio::TcpSocket::failed_handler(const boost::system::error_code& ec)
{

    log_info << "failed handler " << get_id() << " " << ec
             << " " << socket_.is_open();
    if (get_state() != S_FAILED)
    {
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(ec.value()));
        state_ = S_FAILED;
    }
    else
    {
        // gu_throw_fatal << "failed handler state " << get_state();
    }
}

void gcomm::asio::TcpSocket::connect_handler(const boost::system::error_code& ec)
{
    Critical<Protonet> crit(net_);
    log_info << "connect handler " << get_id() << " " << ec;
    if (ec != 0)
    {
        failed_handler(ec);
        return;
    }
    else
    {
        socket_.set_option(ip::tcp::no_delay(true));
        state_ = S_CONNECTED;
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(ec.value()));
        async_receive();
    }
}

void gcomm::asio::TcpSocket::connect(const URI& uri)
{
    Critical<Protonet> crit(net_);
    Protonet& net(static_cast<Protonet&>(net_));
    ip::tcp::resolver resolver(net.io_service_);
    ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                   uri.get_port());
    ip::tcp::resolver::iterator i(resolver.resolve(query));
    socket_.async_connect(*i, boost::bind(&TcpSocket::connect_handler,
                                          shared_from_this(),
                                          placeholders::error));
    state_ = S_CONNECTING;
}

void gcomm::asio::TcpSocket::close()
{
    // Note: Protonet mutex can be destructed already at this phase...
    // figure out if socket_.close() is safe to call without protection
    // Critical<Protonet> crit(net_);
    log_info << "closing " << get_id() << " state " << get_state();

    try
    {
        socket_.close();
    }
    catch (std::exception& e)
    {
        log_warn << "exception caught while closing: " << e.what();
    }
    state_ = S_CLOSED;
}





void gcomm::asio::TcpSocket::write_handler(const boost::system::error_code& ec,
                                           size_t bytes_transferred)
{
    Critical<Protonet> crit(net_);
    if (ec == 0)
    {
        // log_info << "write handler " << bytes_transferred;
        gcomm_assert(send_q_.empty() == false);
        gcomm_assert(send_q_.front().get_len() >= bytes_transferred);
        if (send_q_.front().get_len() < bytes_transferred)
        {
            log_warn << "short write";
            return;
        }
        else
        {
            send_q_.pop_front();
        }
        if (send_q_.empty() == false)
        {
            Datagram& dg(send_q_.front());
            array<const_buffer, 2> cbs;
            cbs[0] = const_buffer(dg.get_header()
                                  + dg.get_header_offset(),
                                  dg.get_header_len());
            cbs[1] = const_buffer(&dg.get_payload()[0],
                                  dg.get_payload().size());

            async_write(socket_, cbs, boost::bind(&TcpSocket::write_handler,
                                                  shared_from_this(),
                                                  placeholders::error,
                                                  placeholders::bytes_transferred));
        }
    }
    else
    {
        log_warn << "write handler: " << ec;
    }
}


int gcomm::asio::TcpSocket::send(const Datagram& dg)
{
    gcomm_assert(get_state() == S_CONNECTED);
    ip::tcp::no_delay no_delay;
    socket_.get_option(no_delay);
    gcomm_assert(no_delay.value() == true);

    uint32_t len(static_cast<uint32_t>(dg.get_len()));
    if (dg.get_header_offset() < sizeof(len))
    {
        gu_throw_fatal;
    }

    Datagram priv_dg(dg);

    priv_dg.set_header_offset(priv_dg.get_header_offset() - sizeof(len));
    serialize(len,
              priv_dg.get_header(),
              priv_dg.get_header_size(),
              priv_dg.get_header_offset());
    send_q_.push_back(priv_dg);

    array<const_buffer, 2> cbs;
    cbs[0] = const_buffer(priv_dg.get_header()
                               + priv_dg.get_header_offset(),
                               priv_dg.get_header_len());
    cbs[1] = const_buffer(&priv_dg.get_payload()[0],
                          priv_dg.get_payload().size());

    if (send_q_.size() == 1)
    {
        async_write(socket_, cbs, boost::bind(&TcpSocket::write_handler,
                                              shared_from_this(),
                                              placeholders::error,
                                              placeholders::bytes_transferred));
    }
    return 0;
}


void gcomm::asio::TcpSocket::read_handler(const boost::system::error_code& ec,
                                          const size_t bytes_transferred)
{
    Critical<Protonet> crit(net_);
    if (ec != 0)
    {
        failed_handler(ec);
        return;
    }

    recv_offset_ += bytes_transferred;

    // log_info << "read handler " << recv_offset_ << " " << bytes_transferred;
    while (recv_offset_ >= 4)
    {
        uint32_t len;
        unserialize(&recv_buf_[0], recv_buf_.size(), 0, &len);

        // log_info << "read handler " << bytes_transferred << " " << len;

        if (recv_offset_ >= len + 4)
        {
            Datagram dg(SharedBuffer(
                            new Buffer(&recv_buf_[0] + 4,
                                       &recv_buf_[0] + 4 + len)));
            ProtoUpMeta um;
            net_.dispatch(get_id(), dg, um);
            recv_offset_ -= 4 + len;

            if (recv_offset_ > 0)
            {
                memmove(&recv_buf_[0], &recv_buf_[0] + 4 + len, recv_offset_);
            }
        }
        else
        {
            break;
        }
    }

    array<mutable_buffer, 1> mbs;
    mbs[0] = mutable_buffer(&recv_buf_[0] + recv_offset_,
                            recv_buf_.size() - recv_offset_);
    async_read(socket_, mbs,
               boost::bind(&TcpSocket::read_completion_condition,
                           shared_from_this(),
                           placeholders::error,
                           placeholders::bytes_transferred),
               boost::bind(&TcpSocket::read_handler,
                           shared_from_this(),
                           placeholders::error,
                           placeholders::bytes_transferred));
}

size_t gcomm::asio::TcpSocket::read_completion_condition(
    const boost::system::error_code& ec,
    const size_t bytes_transferred)
{
    Critical<Protonet> crit(net_);
    if (ec != 0)
    {
        failed_handler(ec);
        return 0;
    }

    if (recv_offset_ + bytes_transferred >= 4)
    {
        uint32_t len;
        unserialize(&recv_buf_[0], 4, 0, &len);
        if (recv_offset_ + bytes_transferred >= 4 + len)
        {
            return 0;
        }
    }

    return (recv_buf_.size() - recv_offset_);

}

void gcomm::asio::TcpSocket::async_receive()
{
    gcomm_assert(get_state() == S_CONNECTED);
    ip::tcp::no_delay no_delay;
    socket_.get_option(no_delay);
    gcomm_assert(no_delay.value() == true);
    array<mutable_buffer, 1> mbs;
    mbs[0] = mutable_buffer(&recv_buf_[0], recv_buf_.size());
    async_read(socket_, mbs,
               boost::bind(&TcpSocket::read_completion_condition,
                           shared_from_this(),
                           placeholders::error,
                           placeholders::bytes_transferred),
               boost::bind(&TcpSocket::read_handler,
                           shared_from_this(),
                           placeholders::error,
                           placeholders::bytes_transferred));
}

size_t gcomm::asio::TcpSocket::get_mtu() const
{
    return net_.get_mtu();
}



std::string gcomm::asio::TcpSocket::get_local_addr() const
{
    return "tcp://"
        + escape_addr(socket_.local_endpoint().address())
        + ":"
        + to_string(socket_.local_endpoint().port());
}

std::string gcomm::asio::TcpSocket::get_remote_addr() const
{
    return "tcp://"
        + escape_addr(socket_.remote_endpoint().address())
        + ":"
        + to_string(socket_.remote_endpoint().port());
}




gcomm::asio::TcpAcceptor::TcpAcceptor(asio::Protonet& net, const URI& uri)
    :
    Acceptor        (uri),
    net_            (net),
    acceptor_       (net_.io_service_),
    accepted_socket_()
{

}

gcomm::asio::TcpAcceptor::~TcpAcceptor()
{
    close();
}


void gcomm::asio::TcpAcceptor::accept_handler(
    SocketPtr socket,
    const boost::system::error_code& error)
{
    if (error == 0)
    {
        static_cast<TcpSocket*>(socket.get())->socket_.set_option(ip::tcp::no_delay(true));
        static_cast<TcpSocket*>(socket.get())->state_ = Socket::S_CONNECTED;
        accepted_socket_ = socket;
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(error.value()));
        TcpSocket* new_socket(new TcpSocket(
                                  static_cast<asio::Protonet&>(net_),
                                  URI(scheme_ + "://")));
        acceptor_.async_accept(new_socket->socket_,
                               boost::bind(&TcpAcceptor::accept_handler,
                                           this,
                                           SocketPtr(new_socket),
                                           placeholders::error));
    }
    else
    {
        log_warn << "accept handler: " << error;
    }
}


void gcomm::asio::TcpAcceptor::listen(const URI& uri)
{
    ip::tcp::resolver resolver(net_.io_service_);
    ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                   uri.get_port());
    ip::tcp::resolver::iterator i(resolver.resolve(query));
    acceptor_.open(i->endpoint().protocol());
    acceptor_.set_option(ip::tcp::socket::reuse_address(true));
    acceptor_.bind(*i);
    acceptor_.listen();
    TcpSocket* new_socket(new TcpSocket(net_, uri));
    acceptor_.async_accept(new_socket->socket_,
                           boost::bind(&TcpAcceptor::accept_handler,
                                       this,
                                       SocketPtr(new_socket),
                                       placeholders::error));
}

void gcomm::asio::TcpAcceptor::close()
{
    try
    {
        acceptor_.close();
    }
    catch (...) { }
}


gcomm::SocketPtr gcomm::asio::TcpAcceptor::accept()
{
    accepted_socket_->async_receive();
    return accepted_socket_;
}
