/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#include "asio_tcp.hpp"
#include "asio_addr.hpp"
#include "gcomm/util.hpp"

#include <boost/bind.hpp>
#include <boost/array.hpp>

using namespace std;
using namespace gu;
using namespace gu::net;

#define FAILED_HANDLER(_e) failed_handler(_e, __FUNCTION__, __LINE__)


gcomm::AsioTcpSocket::AsioTcpSocket(AsioProtonet& net, const URI& uri)
    :
    Socket       (uri),
    net_         (net),
    socket_      (net.io_service_),
    send_q_      (),
    recv_buf_    (net_.get_mtu() + NetHeader::serial_size_),
    recv_offset_ (0),
    state_       (S_CLOSED)
{
    log_debug << "ctor for " << get_id();
}

gcomm::AsioTcpSocket::~AsioTcpSocket()
{
    log_debug << "dtor for " << get_id();
    try
    {
        socket_.close();
    }
    catch (...) { }
}

void gcomm::AsioTcpSocket::failed_handler(const asio::error_code& ec,
                                          const std::string& func,
                                          int line)
{
    log_debug << "failed handler from " << func << ":" << line
              << " socket " << get_id() << " " << socket_.native()
              << " error " << ec
              << " " << socket_.is_open() << " state " << get_state();
    try
    {
        log_debug << "local endpoint " << get_local_addr()
                  << " remote endpoint " << get_remote_addr();
    } catch (...) { }
    const State prev_state(get_state());
    if (get_state() != S_CLOSED)
    {
        state_ = S_FAILED;
    }
    if (prev_state != S_FAILED && prev_state != S_CLOSED)
    {
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(ec.value()));
    }
}

void gcomm::AsioTcpSocket::connect_handler(const asio::error_code& ec)
{
    Critical<AsioProtonet> crit(net_);
    log_debug << "connect handler " << get_id() << " " << ec;
    if (ec)
    {
        FAILED_HANDLER(ec);
        return;
    }
    else
    {
        socket_.set_option(asio::ip::tcp::no_delay(true));
        // socket_.set_option(asio::ip::tcp::socket::linger(true, 1));
        log_debug << "socket " << get_id() << " connected, remote endpoint "
                  << get_remote_addr() << " local endpoint "
                  << get_local_addr();
        state_ = S_CONNECTED;
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(ec.value()));
        async_receive();
    }
}

void gcomm::AsioTcpSocket::connect(const URI& uri)
{
    Critical<AsioProtonet> crit(net_);
    asio::ip::tcp::resolver resolver(net_.io_service_);
    asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                         uri.get_port());
    asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
    socket_.async_connect(*i, boost::bind(&AsioTcpSocket::connect_handler,
                                          shared_from_this(),
                                          asio::placeholders::error));
    state_ = S_CONNECTING;
}

void gcomm::AsioTcpSocket::close()
{
    Critical<AsioProtonet> crit(net_);
    if (get_state() == S_CLOSED || get_state() == S_CLOSING) return;

    log_debug << "closing " << get_id() << " state " << get_state()
              << " send_q size " << send_q_.size();

    if (send_q_.empty() == true || get_state() != S_CONNECTED)
    {
        try
        {
            socket_.close();
        }
        catch (...) { }
        state_ = S_CLOSED;
    }
    else
    {
        state_ = S_CLOSING;
    }
}


void gcomm::AsioTcpSocket::write_handler(const asio::error_code& ec,
                                         size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);

    if (get_state() != S_CONNECTED && get_state() != S_CLOSING)
    {
        log_debug << "write handler for " << get_id()
                  << " state " << get_state();
        return;
    }

    if (!ec)
    {
        gcomm_assert(send_q_.empty() == false);
        gcomm_assert(send_q_.front().get_len() >= bytes_transferred);

        while (send_q_.empty() == false &&
               bytes_transferred >= send_q_.front().get_len())
        {
            const Datagram& dg(send_q_.front());
            bytes_transferred -= dg.get_len();
            send_q_.pop_front();
        }
        gcomm_assert(bytes_transferred == 0);

        if (send_q_.empty() == false)
        {
            const Datagram& dg(send_q_.front());
            boost::array<asio::const_buffer, 2> cbs;
            cbs[0] = asio::const_buffer(dg.get_header()
                                        + dg.get_header_offset(),
                                        dg.get_header_len());
            cbs[1] = asio::const_buffer(&dg.get_payload()[0],
                                        dg.get_payload().size());
            async_write(socket_, cbs, boost::bind(&AsioTcpSocket::write_handler,
                                                  shared_from_this(),
                                                  asio::placeholders::error,
                                                  asio::placeholders::bytes_transferred));
        }
        else if (state_ == S_CLOSING)
        {
            log_debug << "deferred close of " << get_id();
            socket_.close();
            state_ = S_CLOSED;
        }
    }
    else if (state_ == S_CLOSING)
    {
        log_debug << "deferred close of " << get_id() << " error " << ec;
        socket_.close();
        state_ = S_CLOSED;
    }
    else
    {
        FAILED_HANDLER(ec);
    }
}


int gcomm::AsioTcpSocket::send(const Datagram& dg)
{
    Critical<AsioProtonet> crit(net_);

    if (get_state() != S_CONNECTED)
    {
        return ENOTCONN;
    }



    NetHeader hdr(static_cast<uint32_t>(dg.get_len()), net_.version_);
    if (net_.checksum_ == true)
    {
        hdr.set_crc32(crc32(dg));
    }

    send_q_.push_back(dg); // makes copy of dg
    Datagram& priv_dg(send_q_.back());

    priv_dg.set_header_offset(priv_dg.get_header_offset() -
                              NetHeader::serial_size_);
    serialize(hdr,
              priv_dg.get_header(),
              priv_dg.get_header_size(),
              priv_dg.get_header_offset());

    if (send_q_.size() == 1)
    {
        boost::array<asio::const_buffer, 2> cbs;
        cbs[0] = asio::const_buffer(priv_dg.get_header()
                                    + priv_dg.get_header_offset(),
                                    priv_dg.get_header_len());
        cbs[1] = asio::const_buffer(&priv_dg.get_payload()[0],
                                    priv_dg.get_payload().size());
        async_write(socket_, cbs, boost::bind(&AsioTcpSocket::write_handler,
                                              shared_from_this(),
                                              asio::placeholders::error,
                                              asio::placeholders::bytes_transferred));
    }
    return 0;
}


void gcomm::AsioTcpSocket::read_handler(const asio::error_code& ec,
                                        const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);

    if (ec)
    {
        FAILED_HANDLER(ec);
        return;
    }

    if (get_state() == S_CLOSING)
    {
        // keep on reading data in case of deferred shutdown too
        boost::array<asio::mutable_buffer, 1> mbs;
        mbs[0] = asio::mutable_buffer(&recv_buf_[0],
                                      recv_buf_.size());
        async_read(socket_, mbs,
                   boost::bind(&AsioTcpSocket::read_completion_condition,
                               shared_from_this(),
                               asio::placeholders::error,
                               asio::placeholders::bytes_transferred),
                   boost::bind(&AsioTcpSocket::read_handler,
                               shared_from_this(),
                               asio::placeholders::error,
                               asio::placeholders::bytes_transferred));
        return;
    }
    else if (get_state() != S_CONNECTED)
    {
        log_debug << "read handler for " << get_id()
                  << " state " << get_state();
        return;
    }

    recv_offset_ += bytes_transferred;

    while (recv_offset_ >= NetHeader::serial_size_)
    {
        NetHeader hdr;
        try
        {
            unserialize(&recv_buf_[0], recv_buf_.size(), 0, hdr);
        }
        catch (Exception& e)
        {
            FAILED_HANDLER(asio::error_code(e.get_errno(),
                                            asio::error::system_category));
            return;
        }
        if (recv_offset_ >= hdr.len() + NetHeader::serial_size_)
        {
            Datagram dg(SharedBuffer(
                            new Buffer(&recv_buf_[0] + NetHeader::serial_size_,
                                       &recv_buf_[0] + NetHeader::serial_size_
                                       + hdr.len())));
            if (net_.checksum_ == true)
            {
#ifdef TEST_NET_CHECKSUM_ERROR
                long rnd(rand());
                if (rnd % 10000 == 0)
                {
                    hdr.set_crc32(static_cast<uint32_t>(rnd));
                }
#endif // TEST_NET_CHECKSUM_ERROR

                if ((hdr.has_crc32() == true && crc32(dg) != hdr.crc32()) ||
                    (hdr.has_crc32() == false && hdr.crc32() != 0))
                {
                    log_warn << "checksum failed, hdr: len=" << hdr.len()
                             << " has_crc32=" << hdr.has_crc32()
                             << " crc32=" << hdr.crc32();
                    FAILED_HANDLER(asio::error_code(EPROTO, asio::error::system_category));
                    return;
                }
            }
            ProtoUpMeta um;
            net_.dispatch(get_id(), dg, um);
            recv_offset_ -= NetHeader::serial_size_ + hdr.len();

            if (recv_offset_ > 0)
            {
                memmove(&recv_buf_[0],
                        &recv_buf_[0] + NetHeader::serial_size_ + hdr.len(),
                        recv_offset_);
            }
        }
        else
        {
            break;
        }
    }

    boost::array<asio::mutable_buffer, 1> mbs;
    mbs[0] = asio::mutable_buffer(&recv_buf_[0] + recv_offset_,
                                  recv_buf_.size() - recv_offset_);
    async_read(socket_, mbs,
               boost::bind(&AsioTcpSocket::read_completion_condition,
                           shared_from_this(),
                           asio::placeholders::error,
                           asio::placeholders::bytes_transferred),
               boost::bind(&AsioTcpSocket::read_handler,
                           shared_from_this(),
                           asio::placeholders::error,
                           asio::placeholders::bytes_transferred));
}

size_t gcomm::AsioTcpSocket::read_completion_condition(
    const asio::error_code& ec,
    const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);
    if (ec)
    {
        FAILED_HANDLER(ec);
        return 0;
    }

    if (get_state() == S_CLOSING)
    {
        log_debug << "read completion condition for " << get_id()
                  << " state " << get_state();
        return 0;
    }
    else if (state_ != S_CONNECTED)
    {
        log_debug << "read completion condition for " << get_id()
                  << " state " << get_state();
        return 0;
    }

    if (recv_offset_ + bytes_transferred >= NetHeader::serial_size_)
    {
        NetHeader hdr;
        try
        {
            unserialize(&recv_buf_[0], NetHeader::serial_size_, 0, hdr);
        }
        catch (Exception& e)
        {
            log_warn << "unserialize error " << e.what();
            FAILED_HANDLER(asio::error_code(e.get_errno(),
                                            asio::error::system_category));
            return 0;
        }
        if (recv_offset_ + bytes_transferred >= NetHeader::serial_size_ + hdr.len())
        {
            return 0;
        }
    }

    return (recv_buf_.size() - recv_offset_);
}


void gcomm::AsioTcpSocket::async_receive()
{
    Critical<AsioProtonet> crit(net_);
    gcomm_assert(get_state() == S_CONNECTED);

    boost::array<asio::mutable_buffer, 1> mbs;
    mbs[0] = asio::mutable_buffer(&recv_buf_[0], recv_buf_.size());
    async_read(socket_, mbs,
               boost::bind(&AsioTcpSocket::read_completion_condition,
                           shared_from_this(),
                           asio::placeholders::error,
                           asio::placeholders::bytes_transferred),
               boost::bind(&AsioTcpSocket::read_handler,
                           shared_from_this(),
                           asio::placeholders::error,
                           asio::placeholders::bytes_transferred));
}

size_t gcomm::AsioTcpSocket::get_mtu() const
{
    return net_.get_mtu();
}



std::string gcomm::AsioTcpSocket::get_local_addr() const
{
    return "tcp://"
        + escape_addr(socket_.local_endpoint().address())
        + ":"
        + to_string(socket_.local_endpoint().port());
}

std::string gcomm::AsioTcpSocket::get_remote_addr() const
{
    return "tcp://"
        + escape_addr(socket_.remote_endpoint().address())
        + ":"
        + to_string(socket_.remote_endpoint().port());
}




gcomm::AsioTcpAcceptor::AsioTcpAcceptor(AsioProtonet& net, const URI& uri)
    :
    Acceptor        (uri),
    net_            (net),
    acceptor_       (net_.io_service_),
    accepted_socket_()
{

}

gcomm::AsioTcpAcceptor::~AsioTcpAcceptor()
{
    close();
}


void gcomm::AsioTcpAcceptor::accept_handler(
    SocketPtr socket,
    const asio::error_code& error)
{
    if (!error)
    {
        AsioTcpSocket* s(static_cast<AsioTcpSocket*>(socket.get()));
        s->socket_.set_option(asio::ip::tcp::no_delay(true));
        // s->socket_.set_option(asio::ip::tcp::socket::linger(true, 1));
        s->state_ = Socket::S_CONNECTED;
        accepted_socket_ = socket;
        log_debug << "accepted socket " << socket->get_id();
        net_.dispatch(get_id(), Datagram(), ProtoUpMeta(error.value()));
        AsioTcpSocket* new_socket(new AsioTcpSocket(
                                      net_,
                                      URI(scheme_ + "://")));
        acceptor_.async_accept(new_socket->socket_,
                               boost::bind(&AsioTcpAcceptor::accept_handler,
                                           this,
                                           SocketPtr(new_socket),
                                           asio::placeholders::error));
    }
    else
    {
        log_warn << "accept handler: " << error;
    }
}


void gcomm::AsioTcpAcceptor::listen(const URI& uri)
{
    asio::ip::tcp::resolver resolver(net_.io_service_);
    asio::ip::tcp::resolver::query query(unescape_addr(uri.get_host()),
                                   uri.get_port());
    asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
    acceptor_.open(i->endpoint().protocol());
    acceptor_.set_option(asio::ip::tcp::socket::reuse_address(true));
    acceptor_.bind(*i);
    acceptor_.listen();
    AsioTcpSocket* new_socket(new AsioTcpSocket(net_, uri));
    acceptor_.async_accept(new_socket->socket_,
                           boost::bind(&AsioTcpAcceptor::accept_handler,
                                       this,
                                       SocketPtr(new_socket),
                                       asio::placeholders::error));
}

void gcomm::AsioTcpAcceptor::close()
{
    try
    {
        acceptor_.close();
    }
    catch (...) { }
}


gcomm::SocketPtr gcomm::AsioTcpAcceptor::accept()
{
    accepted_socket_->async_receive();
    return accepted_socket_;
}
