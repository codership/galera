/*
 * Copyright (C) 2012-2019 Codership Oy <info@codership.com>
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

        socket.set_option(asio::socket_base::receive_buffer_size(recv_buf_size));
        asio::socket_base::receive_buffer_size option;
        socket.get_option(option);
        log_debug << "socket recv buf size " << option.value();
        if (option.value() < ssize_t(recv_buf_size) && not asio_recv_buf_warned)
        {
            log_warn << "Receive buffer size " << option.value()
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

        socket.set_option(asio::socket_base::send_buffer_size(send_buf_size));
        asio::socket_base::send_buffer_size option;
        socket.get_option(option);
        log_debug << "socket send buf size " << option.value();
        if (option.value() < ssize_t(send_buf_size) && not asio_send_buf_warned)
        {
            log_warn << "Send buffer size " << option.value()
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
    socket_      (net.io_service_),
    ssl_socket_  (0),
    send_q_      (),
    last_queued_tstamp_(),
    recv_buf_    (net_.mtu() + NetHeader::serial_size_),
    recv_offset_ (0),
    last_delivered_tstamp_(),
    state_       (S_CLOSED),
    local_addr_  (),
    remote_addr_ ()
{
    log_debug << "ctor for " << id();
}

gcomm::AsioTcpSocket::~AsioTcpSocket()
{
    log_debug << "dtor for " << id() << " send q size " << send_q_.size();
    close_socket();
    delete ssl_socket_;
    ssl_socket_ = 0;
}

void gcomm::AsioTcpSocket::failed_handler(const asio::error_code& ec,
                                          const std::string& func,
                                          int line)
{
    log_debug << "failed handler from " << func << ":" << line
              << " socket " << id() << " " << socket_.native()
              << " error " << ec
              << " " << socket_.is_open() << " state " << state();

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

void gcomm::AsioTcpSocket::handshake_handler(const asio::error_code& ec)
{
    if (ec)
    {
        if (ec.category() == asio::error::get_ssl_category() &&
            gu::exclude_ssl_error(ec) == false)
        {

            log_error << "handshake with remote endpoint "
                      << remote_addr() << " failed: " << ec << ": '"
                      << ec.message()
                      << "' ( " << gu::extra_error_info(ec) << ")";
        }
        FAILED_HANDLER(ec);
        return;
    }

    if (ssl_socket_ == 0)
    {
        log_error << "handshake handler called for non-SSL socket "
                  << id() << " "
                  << remote_addr() << " <-> "
                  << local_addr();
        FAILED_HANDLER(asio::error_code(EPROTO, asio::error::system_category));
        return;
    }

    const char* compression_name = gu::compression(*ssl_socket_);

    log_info << "SSL handshake successful, "
             << "remote endpoint " << remote_addr()
             << " local endpoint " << local_addr()
             << " cipher: " << gu::cipher(*ssl_socket_)
             << " compression: "
             << (compression_name != NULL ? compression_name : "none");
    state_ = S_CONNECTED;
    init_tstamps();
    net_.dispatch(id(), Datagram(), ProtoUpMeta(ec.value()));
    async_receive();
}

void gcomm::AsioTcpSocket::connect_handler(const asio::error_code& ec)
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
            assign_local_addr();
            assign_remote_addr();
            set_socket_options();
            if (ssl_socket_ != 0)
            {
                log_debug << "socket " << id() << " connected, remote endpoint "
                          << remote_addr() << " local endpoint "
                          << local_addr();
                ssl_socket_->async_handshake(
                    asio::ssl::stream<asio::ip::tcp::socket>::client,
                    boost::bind(&AsioTcpSocket::handshake_handler,
                                shared_from_this(),
                                asio::placeholders::error)
                    );
            }
            else
            {
                log_debug << "socket " << id() << " connected, remote endpoint "
                          << remote_addr() << " local endpoint "
                          << local_addr();
                state_ = S_CONNECTED;
                init_tstamps();
                net_.dispatch(id(), Datagram(), ProtoUpMeta(ec.value()));
                async_receive();

            }
        }
    }
    catch (asio::system_error& e)
    {
        FAILED_HANDLER(e.code());
    }
}

void gcomm::AsioTcpSocket::connect(const gu::URI& uri)
{
    try
    {
        Critical<AsioProtonet> crit(net_);

        asio::ip::tcp::resolver resolver(net_.io_service_);
        // Give query flags explicitly to avoid having AI_ADDRCONFIG in
        // underlying getaddrinfo() hint flags.
        asio::ip::tcp::resolver::query
            query(gu::unescape_addr(uri.get_host()),
                  uri.get_port(),
                  asio::ip::tcp::resolver::query::flags(0));
        asio::ip::tcp::resolver::iterator i(resolver.resolve(query));

        if (uri.get_scheme() == gu::scheme::ssl)
        {
            ssl_socket_ = new asio::ssl::stream<asio::ip::tcp::socket>(
                net_.io_service_, net_.ssl_context_
            );

            ssl_socket_->lowest_layer().open(i->endpoint().protocol());
            set_buf_sizes(); // Must be done before connect
            ssl_socket_->lowest_layer().async_connect(
                *i, boost::bind(&AsioTcpSocket::connect_handler,
                                shared_from_this(),
                                asio::placeholders::error)
            );
        }
        else
        {
            const std::string bind_ip(uri.get_option(
                                          gcomm::Socket::OptIfAddr, ""));
            socket_.open(i->endpoint().protocol());
            if (!bind_ip.empty())
            {
                asio::ip::tcp::endpoint ep(gu::make_address(bind_ip), 0);
                socket_.bind(ep);
            }
            set_buf_sizes(); // Must be done before connect
            socket_.async_connect(*i, boost::bind(&AsioTcpSocket::connect_handler,
                                                  shared_from_this(),
                                                  asio::placeholders::error));
        }
        state_ = S_CONNECTING;
    }
    catch (asio::system_error& e)
    {
        std::ostringstream msg;
        msg << "error while connecting to remote host "
            << uri.to_string()
            << "', asio error '" << e.what() << "'";
        log_warn << msg.str();
        gu_throw_error(e.code().value()) << msg.str();
    }
}

void gcomm::AsioTcpSocket::close()
{
    Critical<AsioProtonet> crit(net_);

    if (state() == S_CLOSED || state() == S_CLOSING) return;

    log_debug << "closing " << id() << " state " << state()
              << " send_q size " << send_q_.size();

    if (send_q_.empty() == true || state() != S_CONNECTED)
    {
        close_socket();
        state_ = S_CLOSED;
    }
    else
    {
        state_ = S_CLOSING;
    }
}

// Enable to introduce random errors for write handler
// #define GCOMM_ASIO_TCP_SIMULATE_WRITE_HANDLER_ERROR

void gcomm::AsioTcpSocket::write_handler(const asio::error_code& ec,
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
        if (ec.category() == asio::error::get_ssl_category() &&
            gu::exclude_ssl_error(ec) == false)
        {
            log_warn << "write_handler(): " << ec.message()
                     << " (" << gu::extra_error_info(ec) << ")";
        }
        return;
    }

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
            FAILED_HANDLER(asio::error_code(EPROTO,
                                            asio::error::system_category));
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
            FAILED_HANDLER(asio::error_code(EPROTO,
                                            asio::error::system_category));
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
                FAILED_HANDLER(asio::error_code(EPROTO,
                                                asio::error::system_category));
            }
            else if (send_q_.empty() == false)
            {
                const Datagram& dg(send_q_.front());
                gu::array<asio::const_buffer, 2>::type cbs;
                cbs[0] = asio::const_buffer(dg.header()
                                            + dg.header_offset(),
                                            dg.header_len());
                cbs[1] = asio::const_buffer(dg.payload().data(),
                                            dg.payload().size());
                write_one(cbs);
            }
            else if (state_ == S_CLOSING)
            {
                log_debug << "deferred close of " << id();
                close_socket();
                state_ = S_CLOSED;
            }
        }
    }
    else if (state_ == S_CLOSING)
    {
        log_debug << "deferred close of " << id() << " error " << ec;
        close_socket();
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
    typedef gu::shared_ptr<gcomm::AsioTcpSocket>::type AsioTcpSocketPtr;
    class AsioPostForSendHandler
    {
    public:
        AsioPostForSendHandler(const AsioTcpSocketPtr& socket)
            :
            socket_(socket)
        { }
        void operator()()
        {
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
                gu::array<asio::const_buffer, 2>::type cbs;
                cbs[0] = asio::const_buffer(dg.header()
                                            + dg.header_offset(),
                                            dg.header_len());
                cbs[1] = asio::const_buffer(dg.payload().data(),
                                            dg.payload().size());
                socket_->write_one(cbs);
            }
        }
    private:
        AsioTcpSocketPtr socket_;
    };
}

int gcomm::AsioTcpSocket::send(int segment, const Datagram& dg)
{
    Critical<AsioProtonet> crit(net_);

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


void gcomm::AsioTcpSocket::read_handler(const asio::error_code& ec,
                                        const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);

    if (ec)
    {
        if (ec.category() == asio::error::get_ssl_category() &&
            gu::exclude_ssl_error(ec) == false)
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
            FAILED_HANDLER(asio::error_code(e.get_errno(),
                                            asio::error::system_category));
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
                    FAILED_HANDLER(asio::error_code(
                                       EPROTO,
                                       asio::error::system_category));
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

    gu::array<asio::mutable_buffer, 1>::type mbs;
    mbs[0] = asio::mutable_buffer(&recv_buf_[0] + recv_offset_,
                                  recv_buf_.size() - recv_offset_);
    read_one(mbs);
}

size_t gcomm::AsioTcpSocket::read_completion_condition(
    const asio::error_code& ec,
    const size_t bytes_transferred)
{
    Critical<AsioProtonet> crit(net_);
    if (ec)
    {
        if (ec.category() == asio::error::get_ssl_category() &&
            gu::exclude_ssl_error(ec) == false)
        {
            log_warn << "read_completion_condition(): " << ec.message() << " ("
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
        catch (gu::Exception& e)
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

    gcomm_assert(state() == S_CONNECTED);

    gu::array<asio::mutable_buffer, 1>::type mbs;

    mbs[0] = asio::mutable_buffer(&recv_buf_[0], recv_buf_.size());
    read_one(mbs);
}

size_t gcomm::AsioTcpSocket::mtu() const
{
    return net_.mtu();
}



std::string gcomm::AsioTcpSocket::local_addr() const
{
    return local_addr_;
}

std::string gcomm::AsioTcpSocket::remote_addr() const
{
    return remote_addr_;
}

void gcomm::AsioTcpSocket::set_socket_options()
{
    basic_socket_t& sock(socket());
    gu::set_fd_options(sock);
    sock.set_option(asio::ip::tcp::no_delay(true));
}

void gcomm::AsioTcpSocket::set_buf_sizes()
{
    set_recv_buf_size_helper(net_.conf(), socket());
    set_send_buf_size_helper(net_.conf(), socket());
}

void gcomm::AsioTcpSocket::read_one(
    gu::array<asio::mutable_buffer, 1>::type& mbs)
{
    if (ssl_socket_ != 0)
    {
        async_read(*ssl_socket_, mbs,
                   boost::bind(&AsioTcpSocket::read_completion_condition,
                               shared_from_this(),
                               asio::placeholders::error,
                               asio::placeholders::bytes_transferred),
                   boost::bind(&AsioTcpSocket::read_handler,
                               shared_from_this(),
                               asio::placeholders::error,
                               asio::placeholders::bytes_transferred));
    }
    else
    {
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
}


void gcomm::AsioTcpSocket::write_one(
    const gu::array<asio::const_buffer, 2>::type& cbs)
{
    if (ssl_socket_ != 0)
    {
        async_write(*ssl_socket_, cbs,
                    boost::bind(&AsioTcpSocket::write_handler,
                                shared_from_this(),
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred));
    }
    else
    {
        async_write(socket_, cbs,
                    boost::bind(&AsioTcpSocket::write_handler,
                                shared_from_this(),
                                asio::placeholders::error,
                                asio::placeholders::bytes_transferred));
    }
}


void gcomm::AsioTcpSocket::close_socket()
{
    try
    {
        if (ssl_socket_ != 0)
        {
            // close underlying transport before calling shutdown()
            // to avoid blocking
            ssl_socket_->lowest_layer().close();
            ssl_socket_->shutdown();
        }
        else
        {
            socket_.close();
        }
    }
    catch (...) { }
}

void gcomm::AsioTcpSocket::assign_local_addr()
{
    if (ssl_socket_ != 0)
    {
        local_addr_ = gcomm::uri_string(
            gu::scheme::ssl,
            gu::escape_addr(
                ssl_socket_->lowest_layer().local_endpoint().address()),
            gu::to_string(
                ssl_socket_->lowest_layer().local_endpoint().port())
            );
    }
    else
    {
        local_addr_ = gcomm::uri_string(
            gu::scheme::tcp,
            gu::escape_addr(socket_.local_endpoint().address()),
            gu::to_string(socket_.local_endpoint().port())
            );
    }
}

void gcomm::AsioTcpSocket::assign_remote_addr()
{
    if (ssl_socket_ != 0)
    {
        remote_addr_ = gcomm::uri_string(
            gu::scheme::ssl,
            gu::escape_addr(
                ssl_socket_->lowest_layer().remote_endpoint().address()),
            gu::to_string(
                ssl_socket_->lowest_layer().remote_endpoint().port())
            );
    }
    else
    {
        remote_addr_ = uri_string(
            gu::scheme::tcp,
            gu::escape_addr(socket_.remote_endpoint().address()),
            gu::to_string(socket_.remote_endpoint().port())
            );
    }
}

gcomm::SocketStats gcomm::AsioTcpSocket::stats() const
{
    SocketStats ret;
#if defined(__linux__) || defined(__FreeBSD__)
    struct tcp_info tcpi;
    memset(&tcpi, 0, sizeof(tcpi));
    socklen_t tcpi_len(sizeof(tcpi));
    int native_fd(ssl_socket_ ?
                  const_cast<basic_socket_t&>(ssl_socket_->lowest_layer()).native() :
                  const_cast<asio::ip::tcp::socket&>(socket_).native());
#if defined(__linux__)
    int level(SOL_TCP);
#else
    int level(IPPROTO_TCP);
#endif /* __linux__ */
    if (getsockopt(native_fd, level, TCP_INFO, &tcpi, &tcpi_len) == 0)
    {
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
#endif /* __linux__ || __FreeBSD__ */
    return ret;
}

gcomm::AsioTcpAcceptor::AsioTcpAcceptor(AsioProtonet& net, const gu::URI& uri)
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
        try
        {
            s->assign_local_addr();
            s->assign_remote_addr();
            s->set_socket_options();
            if (s->ssl_socket_ != 0)
            {
                log_debug << "socket "
                          << s->id() << " connected, remote endpoint "
                          << s->remote_addr() << " local endpoint "
                          << s->local_addr();
                s->ssl_socket_->async_handshake(
                    asio::ssl::stream<asio::ip::tcp::socket>::server,
                    boost::bind(&AsioTcpSocket::handshake_handler,
                                s->shared_from_this(),
                                asio::placeholders::error));
                s->state_ = Socket::S_CONNECTING;
            }
            else
            {
                s->state_ = Socket::S_CONNECTED;
            }
            accepted_socket_ = socket;
            log_debug << "accepted socket " << socket->id();
            net_.dispatch(id(), Datagram(), ProtoUpMeta(error.value()));
        }
        catch (asio::system_error& e)
        {
            // socket object should be freed automatically when it
            // goes out of scope
            log_debug << "accept failed: " << e.what();
        }
        AsioTcpSocket* new_socket(new AsioTcpSocket(net_, uri_));
        if (uri_.get_scheme() == gu::scheme::ssl)
        {
            new_socket->ssl_socket_ =
                new asio::ssl::stream<asio::ip::tcp::socket>(
                    net_.io_service_, net_.ssl_context_);
        }
        acceptor_.async_accept(new_socket->socket(),
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

void gcomm::AsioTcpAcceptor::set_buf_sizes()
{
    set_recv_buf_size_helper(net_.conf(), acceptor_);
    set_send_buf_size_helper(net_.conf(), acceptor_);
}


void gcomm::AsioTcpAcceptor::listen(const gu::URI& uri)
{
    try
    {
        asio::ip::tcp::resolver resolver(net_.io_service_);
        // Give query flags explicitly to avoid having AI_ADDRCONFIG in
        // underlying getaddrinfo() hint flags.
        asio::ip::tcp::resolver::query query(gu::unescape_addr(uri.get_host()),
                                             uri.get_port(),
                                             asio::ip::tcp::resolver::query::flags(0));
        asio::ip::tcp::resolver::iterator i(resolver.resolve(query));
        acceptor_.open(i->endpoint().protocol());
        acceptor_.set_option(asio::ip::tcp::socket::reuse_address(true));
        gu::set_fd_options(acceptor_);
        set_buf_sizes(); // Must be done before listen
        acceptor_.bind(*i);
        acceptor_.listen();
        AsioTcpSocket* new_socket(new AsioTcpSocket(net_, uri));
        if (uri_.get_scheme() == gu::scheme::ssl)
        {
            new_socket->ssl_socket_ =
                new asio::ssl::stream<asio::ip::tcp::socket>(
                    net_.io_service_, net_.ssl_context_);
        }
        acceptor_.async_accept(new_socket->socket(),
                               boost::bind(&AsioTcpAcceptor::accept_handler,
                                           this,
                                           SocketPtr(new_socket),
                                           asio::placeholders::error));
    }
    catch (asio::system_error& e)
    {
        std::ostringstream msg;
        msg << "error while trying to listen '" << uri.to_string()
            << "', asio error '" << e.what() << "'";
        log_warn << msg.str();
        gu_throw_error(e.code().value()) << msg.str();
    }
}


std::string gcomm::AsioTcpAcceptor::listen_addr() const
{
    try
    {
        return uri_string(
                   uri_.get_scheme(),
                   gu::escape_addr(acceptor_.local_endpoint().address()),
                   gu::to_string(acceptor_.local_endpoint().port())
               );
    }
    catch (asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "failed to read listen addr "
            << "', asio error '" << e.what() << "'";
    }
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
    if (accepted_socket_->state() == Socket::S_CONNECTED)
    {
        accepted_socket_->async_receive();
    }
    return accepted_socket_;
}
