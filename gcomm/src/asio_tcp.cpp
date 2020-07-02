/*
 * Copyright (C) 2012-2020 Codership Oy <info@codership.com>
 */

#include "asio_tcp.hpp"
#include "gcomm/util.hpp"
#include "gcomm/common.hpp"

#define FAILED_HANDLER(_e) failed_handler(_e, __FUNCTION__, __LINE__)

// Helpers to set socket buffer sizes for both connecting
// and listening sockets.

static bool asio_recv_buf_warned(false);
template <class Socket>
void set_recv_buf_size_helper(const gu::Config& conf, Socket& socket)
{
    if (conf.get(gcomm::Conf::SocketRecvBufSize) != GCOMM_ASIO_AUTO_BUF_SIZE)
    {
        size_t const recv_buf_size
            (conf.get<size_t>(gcomm::Conf::SocketRecvBufSize));
        // this should have been checked already
        assert(ssize_t(recv_buf_size) >= 0);

        socket->set_receive_buffer_size(recv_buf_size);
        auto cur_value(socket->get_receive_buffer_size());
        log_debug << "socket recv buf size " << cur_value;
        if (cur_value < recv_buf_size && not asio_recv_buf_warned)
        {
            log_warn << "Receive buffer size " << cur_value
                     << " less than requested " << recv_buf_size
                     << ", this may affect performance in high latency/high "
                     << "throughput networks.";
            asio_recv_buf_warned = true;
        }
    }
}

static bool asio_send_buf_warned(false);
template <class Socket>
void set_send_buf_size_helper(const gu::Config& conf, Socket& socket)
{
    if (conf.get(gcomm::Conf::SocketSendBufSize) != GCOMM_ASIO_AUTO_BUF_SIZE)
    {
        size_t const send_buf_size
            (conf.get<size_t>(gcomm::Conf::SocketSendBufSize));
        // this should have been checked already
        assert(ssize_t(send_buf_size) >= 0);

        socket->set_send_buffer_size(send_buf_size);
        auto cur_value(socket->get_send_buffer_size());
        log_debug << "socket send buf size " << cur_value;
        if (cur_value < send_buf_size && not asio_send_buf_warned)
        {
            log_warn << "Send buffer size " << cur_value
                     << " less than requested " << send_buf_size
                     << ", this may affect performance in high latency/high "
                     << "throughput networks.";
            asio_send_buf_warned = true;
        }
    }
}

gcomm::AsioTcpSocket::AsioTcpSocket(AsioProtonet& net, const gu::URI& uri)
    :
    Socket       (uri),
    net_         (net),
    socket_      (net.io_service_.make_socket(uri)),
    send_q_      (),
    last_queued_tstamp_(),
    recv_buf_    (net_.mtu() + NetHeader::serial_size_),
    recv_offset_ (0),
    last_delivered_tstamp_(),
    state_       (S_CLOSED),
    deferred_close_timer_()
{
    log_debug << "ctor for " << id();
}

gcomm::AsioTcpSocket::AsioTcpSocket(AsioProtonet& net,
                                    const gu::URI& uri,
                                    const std::shared_ptr<gu::AsioSocket>& socket)
    :
    Socket       (uri),
    net_         (net),
    socket_      (socket),
    send_q_      (),
    last_queued_tstamp_(),
    recv_buf_    (net_.mtu() + NetHeader::serial_size_),
    recv_offset_ (0),
    last_delivered_tstamp_(),
    state_       (S_CLOSED),
    deferred_close_timer_()
{
    log_debug << "ctor for " << id();
}

gcomm::AsioTcpSocket::~AsioTcpSocket()
{
    log_debug << "dtor for " << id() << " state " << state_
             << " send q size " << send_q_.size();
    if (state_ != S_CLOSED)
    {
        socket_->close();
    }
}

void gcomm::AsioTcpSocket::failed_handler(const gu::AsioErrorCode& ec,
                                          const std::string& func,
                                          int line)
{
    log_debug << "failed handler from " << func << ":" << line
              << " socket " << id()
              << " error " << ec
              << " " << socket_->is_open() << " state " << state();

    try
    {
        log_debug << "local endpoint " << local_addr()
                  << " remote endpoint " << remote_addr();
    } catch (...) { }

    const State prev_state(state());

    if (state() != S_CLOSED)
    {
        state_ = S_FAILED;
    }

    if (prev_state != S_FAILED && prev_state != S_CLOSED)
    {
        net_.dispatch(id(), Datagram(), ProtoUpMeta(ec.value()));
    }
}

void gcomm::AsioTcpSocket::connect_handler(gu::AsioSocket& socket,
                                           const gu::AsioErrorCode& ec)
{
    Critical<AsioProtonet> crit(net_);

    try
    {
        if (ec)
        {
            FAILED_HANDLER(ec);
            return;
        }
        else
        {
            state_ = S_CONNECTED;
            init_tstamps();
            net_.dispatch(id(), Datagram(), ProtoUpMeta(ec.value()));
            async_receive();
        }
    }
    catch (const gu::Exception& e)
    {
        FAILED_HANDLER(gu::AsioErrorCode(e.get_errno()));
    }
}

void gcomm::AsioTcpSocket::connect(const gu::URI& uri)
{
    try
    {
        Critical<AsioProtonet> crit(net_);

        socket_->open(uri);


        set_buf_sizes(); // Must be done before connect
        const std::string bind_ip(uri.get_option(gcomm::Socket::OptIfAddr, ""));
        if (not bind_ip.empty())
        {
            socket_->bind(gu::make_address(bind_ip));
        }

        socket_->async_connect(uri, shared_from_this());
        state_ = S_CONNECTING;
    }
    catch (const gu::Exception& e)
    {
        std::ostringstream msg;
        msg << "error while connecting to remote host "
            << uri.to_string()
            << "', asio error '" << e.what() << "'";
        log_warn << msg.str();
        gu_throw_error(e.get_errno()) << msg.str();
    }
}

#include "gu_disable_non_virtual_dtor.hpp"

// Helper class to keep the socket open for writing remaining messages
// after gcomm::AsioTcpSocket::close() has been called.
// The socket is kept open until all queued messages have been written
// or timeout occurs. This is achieved by storing shared pointer
// of the socket into timer object.
class gcomm::AsioTcpSocket::DeferredCloseTimer
    : public gu::AsioSteadyTimerHandler
    , public std::enable_shared_from_this<DeferredCloseTimer>
{
public:
    DeferredCloseTimer(gu::AsioIoService& io_service,
                       const std::shared_ptr<AsioTcpSocket>& socket)
        : socket_(socket)
        , io_service_(io_service)
        , timer_(io_service_)
    {
    }

    ~DeferredCloseTimer()
    {
        log_info << "Deferred close timer destruct";
    }

    void start()
    {
        timer_.expires_from_now(std::chrono::seconds(5));
        timer_.async_wait(shared_from_this());
        log_info << "Deferred close timer started for socket with "
                 << "remote endpoint: " << socket_->remote_addr();
    }

    void cancel()
    {
        log_debug << "Deferred close timer cancel " << socket_->socket_;
        timer_.cancel();
    }

    virtual void handle_wait(const gu::AsioErrorCode& ec) GALERA_OVERRIDE
    {
        log_info << "Deferred close timer handle_wait "
                 << ec << " for " << socket_->socket_;
        socket_->close();
        socket_.reset();
    }

private:
    std::shared_ptr<AsioTcpSocket> socket_;
    gu::AsioIoService& io_service_;
    gu::AsioSteadyTimer timer_;
};

#include "gu_enable_non_virtual_dtor.hpp"


void gcomm::AsioTcpSocket::close()
{
    Critical<AsioProtonet> crit(net_);

    if (state() == S_CLOSED || state() == S_CLOSING) return;

    log_debug << "closing " << id()
              << " socket " << socket_
              << " state " << state()
              << " send_q size " << send_q_.size();

    if (send_q_.empty() == true || state() != S_CONNECTED)
    {
        socket_->close();
        state_ = S_CLOSED;
    }
    else
    {
        state_ = S_CLOSING;
        auto timer(std::make_shared<DeferredCloseTimer>(
                       net_.io_service_, shared_from_this()));
        deferred_close_timer_ = timer;
        timer->start();
    }
}

// Enable to introduce random errors for write handler
// #define GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR

void gcomm::AsioTcpSocket::write_handler(gu::AsioSocket& socket,
                                         const gu::AsioErrorCode& ec,
                                         size_t bytes_transferred)
{
#ifdef GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
    static const long empty_rate(10000);
    static const long bytes_transferred_less_than_rate(10000);
    static const long bytes_transferred_not_zero_rate(10000);
#endif // GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR

    Critical<AsioProtonet> crit(net_);

    if (state() != S_CONNECTED && state() != S_CLOSING)
    {
        log_debug << "write handler for " << id()
                  << " state " << state();
        if (not gu::is_verbose_error(ec))
        {
            log_warn << "write_handler(): " << ec.message()
                     << " (" << gu::extra_error_info(ec) << ")";
        }
        return;
    }

    log_debug << "gcomm::AsioTcpSocket::write_handler() ec " << ec << " socket "
              << socket_ << " send_q " << send_q_.size();
    if (!ec)
    {
        if (send_q_.empty() == true
#ifdef GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
            || ::rand() % empty_rate == 0
#endif // GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
            )
        {
            log_warn << "write_handler() called with empty send_q_. "
                     << "Transport may not be reliable, closing the socket";
            FAILED_HANDLER(gu::AsioErrorCode(EPROTO));
        }
        else if (send_q_.front().len() < bytes_transferred
#ifdef GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
                 || ::rand() % bytes_transferred_less_than_rate == 0
#endif // GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
            )
        {
            log_warn << "write_handler() bytes_transferred "
                     << bytes_transferred
                     << " less than sent "
                     << send_q_.front().len()
                     << ". Transport may not be reliable, closing the socket";
            FAILED_HANDLER(gu::AsioErrorCode(EPROTO));
        }
        else
        {
            while (send_q_.empty() == false &&
                   bytes_transferred >= send_q_.front().len())
            {
                const Datagram& dg(send_q_.front());
                bytes_transferred -= dg.len();
                send_q_.pop_front();
            }
            log_debug << "AsioTcpSocket::write_handler() after queue purge "
                      << socket_
                      << " send_q " << send_q_.size();
            if (bytes_transferred != 0
#ifdef GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
                || ::rand() % bytes_transferred_not_zero_rate == 0
#endif // GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR
                )
            {
                log_warn << "write_handler() bytes_transferred "
                         << bytes_transferred
                         << " after processing the send_q_. "
                         << "Transport may not be reliable, closing the socket";
                FAILED_HANDLER(gu::AsioErrorCode(EPROTO));
            }
            else if (send_q_.empty() == false)
            {
                const Datagram& dg(send_q_.front());
                std::array<gu::AsioConstBuffer, 2> cbs;
                cbs[0] = gu::AsioConstBuffer(dg.header()
                                             + dg.header_offset(),
                                             dg.header_len());
                cbs[1] = gu::AsioConstBuffer(dg.payload().data(),
                                             dg.payload().size());
                socket_->async_write(cbs, shared_from_this());
            }
            else if (state_ == S_CLOSING)
            {
                log_debug << "deferred close of " << id();
                socket_->close();
                // deferred_close_timer_->cancel();
                cancel_deferred_close_timer();
                state_ = S_CLOSED;
            }
        }
    }
    else if (state_ == S_CLOSING)
    {
        log_debug << "deferred close of " << id() << " error " << ec;
        socket_->close();
        // deferred_close_timer_->cancel();
        cancel_deferred_close_timer();
        state_ = S_CLOSED;
    }
    else
    {
        FAILED_HANDLER(ec);
    }
}

void gcomm::AsioTcpSocket::set_option(const std::string& key,
                                      const std::string& val)
{
    // Currently adjustable socket.recv_buf_size and socket.send_buf_size
    // bust be set before the connection is established, so the runtime
    // setting will not be effective.
    log_warn << "Setting " << key << " in run time does not have effect, "
             << "please set the configuration in provider options "
             << "and restart";
}

namespace gcomm
{
    class AsioPostForSendHandler
    {
    public:
        AsioPostForSendHandler(const std::shared_ptr<AsioTcpSocket>& socket)
            :
            socket_(socket)
        { }
        void operator()()
        {
            log_debug << "AsioPostForSendHandler " << socket_->socket_;
            Critical<AsioProtonet> crit(socket_->net_);
            // Send queue is processed also in closing state
            // in order to deliver as many messages as possible,
            // even if the socket has been discarded by
            // upper layers.
            if ((socket_->state() == gcomm::Socket::S_CONNECTED ||
                 socket_->state() == gcomm::Socket::S_CLOSING) &&
                socket_->send_q_.empty() == false)
            {
                const gcomm::Datagram& dg(socket_->send_q_.front());
                std::array<gu::AsioConstBuffer, 2> cbs;
                cbs[0] = gu::AsioConstBuffer(dg.header()
                                             + dg.header_offset(),
                                             dg.header_len());
                cbs[1] = gu::AsioConstBuffer(dg.payload().data(),
                                             dg.payload().size());
                socket_->socket_->async_write(cbs, socket_);
            }
        }
    private:
        std::shared_ptr<AsioTcpSocket> socket_;
    };
}

int gcomm::AsioTcpSocket::send(int segment, const Datagram& dg)
{
    Critical<AsioProtonet> crit(net_);

    log_debug << "AsioTcpSocket::send() socket "
              << socket_ << " state " << state_ << " send_q " << send_q_.size();
    if (state() != S_CONNECTED)
    {
        return ENOTCONN;
    }

    if (send_q_.size() >= max_send_q_bytes)
    {
        return ENOBUFS;
    }

    NetHeader hdr(static_cast<uint32_t>(dg.len()), net_.version_);

    if (net_.checksum_ != NetHeader::CS_NONE)
    {
        hdr.set_crc32(crc32(net_.checksum_, dg), net_.checksum_);
    }

    last_queued_tstamp_ = gu::datetime::Date::monotonic();
    // Make copy of datagram to be able to adjust the header
    Datagram priv_dg(dg);
    priv_dg.set_header_offset(priv_dg.header_offset() -
                              NetHeader::serial_size_);
    serialize(hdr,
              priv_dg.header(),
              priv_dg.header_size(),
              priv_dg.header_offset());
    send_q_.push_back(segment, priv_dg);
    if (send_q_.size() == 1)
    {
        net_.io_service_.post(AsioPostForSendHandler(shared_from_this()));
    }
    return 0;
}


void gcomm::AsioTcpSocket::read_handler(gu::AsioSocket& socket,
                                        const gu::AsioErrorCode& ec,
                                        const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);

    if (ec)
    {
        if (not gu::is_verbose_error(ec))
        {
            log_warn << "read_handler(): " << ec.message() << " ("
                     << gu::extra_error_info(ec) << ")";
        }
        FAILED_HANDLER(ec);
        return;
    }

    if (state() != S_CONNECTED && state() != S_CLOSING)
    {
        log_debug << "read handler for " << id()
                  << " state " << state();
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
        catch (gu::Exception& e)
        {
            FAILED_HANDLER(gu::AsioErrorCode(e.get_errno()));
            return;
        }
        if (recv_offset_ >= hdr.len() + NetHeader::serial_size_)
        {
            Datagram dg(
                gu::SharedBuffer(
                    new gu::Buffer(&recv_buf_[0] + NetHeader::serial_size_,
                                   &recv_buf_[0] + NetHeader::serial_size_
                                   + hdr.len())));
            if (net_.checksum_ != NetHeader::CS_NONE)
            {
#ifdef TEST_NET_CHECKSUM_ERROR
                long rnd(rand());
                if (rnd % 10000 == 0)
                {
                    hdr.set_crc32(net_.checksum_, static_cast<uint32_t>(rnd));
                }
#endif /* TEST_NET_CHECKSUM_ERROR */

                if (check_cs (hdr, dg))
                {
                    log_warn << "checksum failed, hdr: len=" << hdr.len()
                             << " has_crc32="  << hdr.has_crc32()
                             << " has_crc32c=" << hdr.has_crc32c()
                             << " crc32=" << hdr.crc32();
                    FAILED_HANDLER(gu::AsioErrorCode(EPROTO));
                    return;
                }
            }
            ProtoUpMeta um;
            last_delivered_tstamp_ = gu::datetime::Date::monotonic();
            net_.dispatch(id(), dg, um);
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

    if (socket_->is_open())
    {
        socket_->async_read(gu::AsioMutableBuffer(
                                &recv_buf_[0] + recv_offset_,
                                recv_buf_.size() - recv_offset_),
                            shared_from_this());
    }
}

size_t gcomm::AsioTcpSocket::read_completion_condition(
    gu::AsioSocket&,
    const gu::AsioErrorCode& ec,
    const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);
    if (ec)
    {
        if (not gu::is_verbose_error(ec))
        {
            log_warn << "read_completion_condition(): "
                     << ec.message() << " ("
                     << gu::extra_error_info(ec) << ")";
        }
        FAILED_HANDLER(ec);
        return 0;
    }

    if (state() != S_CONNECTED && state() != S_CLOSING)
    {
        log_debug << "read completion condition for " << id()
                  << " state " << state();
        return 0;
    }

    if (recv_offset_ + bytes_transferred >= NetHeader::serial_size_)
    {
        NetHeader hdr;
        try
        {
            unserialize(&recv_buf_[0], NetHeader::serial_size_, 0, hdr);
        }
        catch (const gu::Exception& e)
        {
            log_warn << "unserialize error " << e.what();
            FAILED_HANDLER(gu::AsioErrorCode(e.get_errno()));
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

    gcomm_assert(state() == S_CONNECTED);

    socket_->async_read(gu::AsioMutableBuffer(&recv_buf_[0], recv_buf_.size()),
                        shared_from_this());
}

size_t gcomm::AsioTcpSocket::mtu() const
{
    return net_.mtu();
}



std::string gcomm::AsioTcpSocket::local_addr() const
{
    return socket_->local_addr();
}

std::string gcomm::AsioTcpSocket::remote_addr() const
{
    return socket_->remote_addr();
}

void gcomm::AsioTcpSocket::set_buf_sizes()
{
    set_recv_buf_size_helper(net_.conf(), socket_);
    set_send_buf_size_helper(net_.conf(), socket_);
}

void gcomm::AsioTcpSocket::cancel_deferred_close_timer()
{
    auto timer(deferred_close_timer_.lock());
    if (timer) timer->cancel();
}

gcomm::SocketStats gcomm::AsioTcpSocket::stats() const
{
    SocketStats ret;
    try
    {
        auto tcpi(socket_->get_tcp_info());
        ret.rtt            = tcpi.tcpi_rtt;
        ret.rttvar         = tcpi.tcpi_rttvar;
        ret.rto            = tcpi.tcpi_rto;
#if defined(__linux__)
        ret.lost           = tcpi.tcpi_lost;
#else
        ret.lost           = 0;
#endif /* __linux__ */
        ret.last_data_recv = tcpi.tcpi_last_data_recv;
        ret.cwnd           = tcpi.tcpi_snd_cwnd;
        gu::datetime::Date now(gu::datetime::Date::monotonic());
        Critical<AsioProtonet> crit(net_);
        ret.last_queued_since = (now - last_queued_tstamp_).get_nsecs();
        ret.last_delivered_since = (now - last_delivered_tstamp_).get_nsecs();
        ret.send_queue_length = send_q_.size();
        ret.send_queue_bytes = send_q_.queued_bytes();
        ret.send_queue_segments = send_q_.segments();
    }
    catch (...)
    { }
    return ret;
}

gcomm::AsioTcpAcceptor::AsioTcpAcceptor(AsioProtonet& net, const gu::URI& uri)
    :
    Acceptor        (uri),
    net_            (net),
    acceptor_       (net_.io_service_.make_acceptor(uri)),
    accepted_socket_()
{ }

gcomm::AsioTcpAcceptor::~AsioTcpAcceptor()
{
    close();
}


void gcomm::AsioTcpAcceptor::accept_handler(
    gu::AsioAcceptor&,
    const std::shared_ptr<gu::AsioSocket>& accepted_socket,
    const gu::AsioErrorCode& error)
{
    if (!error)
    {
        auto socket(std::make_shared<AsioTcpSocket>(net_, uri_, accepted_socket));
        socket->state_ = Socket::S_CONNECTED;
        accepted_socket_ = socket;
        log_debug << "accepted socket " << socket->id();
        net_.dispatch(id(), Datagram(), ProtoUpMeta(error.value()));
        acceptor_->async_accept(shared_from_this());
    }
}

void gcomm::AsioTcpAcceptor::set_buf_sizes()
{
    set_recv_buf_size_helper(net_.conf(), acceptor_);
    set_send_buf_size_helper(net_.conf(), acceptor_);
}


void gcomm::AsioTcpAcceptor::listen(const gu::URI& uri)
{
    acceptor_->open(uri);
    set_buf_sizes(); // Must be done before listen
    acceptor_->listen(uri);
    acceptor_->async_accept(shared_from_this());
}

std::string gcomm::AsioTcpAcceptor::listen_addr() const
{
    return acceptor_->listen_addr();
}

void gcomm::AsioTcpAcceptor::close()
{
    acceptor_->close();
}


gcomm::SocketPtr gcomm::AsioTcpAcceptor::accept()
{
    if (accepted_socket_->state() == Socket::S_CONNECTED)
    {
        accepted_socket_->async_receive();
    }
    return accepted_socket_;
}
