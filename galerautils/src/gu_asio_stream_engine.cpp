//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#define GU_ASIO_IMPL

#include "gu_asio_stream_engine.hpp"

#include "gu_asio_io_service_impl.hpp"
#include "gu_asio_debug.hpp"
#include "gu_asio_error_category.hpp"

#include "gu_throw.hpp"
#include "gu_compiler.hpp"

#include "gu_datetime.hpp"

#include <unistd.h>
#include <cassert>

#include <memory>

// Raw TCP stream engine.
class AsioTcpStreamEngine : public gu::AsioStreamEngine
{
public:
    AsioTcpStreamEngine(int fd)
        : fd_(fd)
        , last_error_()
    { }

    virtual std::string scheme() const GALERA_OVERRIDE
    {
        return gu::scheme::tcp;
    }

    virtual enum op_status client_handshake() GALERA_OVERRIDE
    {
        return success;
    }
    virtual enum op_status server_handshake() GALERA_OVERRIDE
    {
        return success;
    }
    virtual void shutdown() GALERA_OVERRIDE { }

    virtual op_result read(void* buf, size_t max_count) GALERA_OVERRIDE
    {
        clear_error();
        ssize_t bytes_read(::read(fd_, buf, max_count));
        if (bytes_read > 0)
        {
            return op_result{success, static_cast<size_t>(bytes_read)};
        }
        else if (bytes_read == 0)
        {
            return op_result{eof, 0};
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return op_result{want_read, 0};
        }
        else
        {
            last_error_ = errno;
            return op_result{error, 0};
        }
    }

    virtual op_result write(const void* buf, size_t count) GALERA_OVERRIDE
    {
        clear_error();
        ssize_t bytes_written(::send(fd_, buf, count, MSG_NOSIGNAL));
        if (bytes_written > 0)
        {
            return op_result{success, static_cast<size_t>(bytes_written) };
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return op_result{want_write, 0};
        }
        else
        {
            last_error_ = errno;
            return op_result{error, 0};
        }
    }

    virtual gu::AsioErrorCode last_error() const GALERA_OVERRIDE
    {
        return gu::AsioErrorCode(last_error_, gu_asio_system_category);
    }
private:
    void clear_error() { last_error_ = 0; }
    int fd_;
    int last_error_;
};

#ifdef GALERA_HAVE_SSL

#include <openssl/ssl.h>

#if OPENSSL_VERSION_NUMBER >= 0x1010100fL
#define HAVE_READ_EX
#define HAVE_WRITE_EX
#endif

class AsioSslStreamEngine : public gu::AsioStreamEngine
{
public:
    AsioSslStreamEngine(gu::AsioIoService& io_service, int fd)
        : fd_(fd)
        , ssl_(::SSL_new(io_service.impl().ssl_context_->native_handle()))
        , last_error_()
        , last_verify_error_()
        , last_error_category_()
    {
        ::SSL_set_fd(ssl_, fd_);
    }

    ~AsioSslStreamEngine()
    {
        ::SSL_free(ssl_);
    }

    AsioSslStreamEngine(const AsioSslStreamEngine&) = delete;
    AsioSslStreamEngine& operator=(const AsioSslStreamEngine&) = delete;

    virtual std::string scheme() const GALERA_OVERRIDE
    {
        return gu::scheme::ssl;
    }

    virtual enum op_status client_handshake() GALERA_OVERRIDE
    {
        clear_error();
        auto result(SSL_connect(ssl_));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::client_handshake: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error);
        return map_status(ssl_error, sys_error, "client_handshake");
    }

    virtual enum op_status server_handshake() GALERA_OVERRIDE
    {
        clear_error();
        auto result(SSL_accept(ssl_));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::server_handshake: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error);
        return map_status(ssl_error, sys_error, "server_handshake");
    }

    virtual void shutdown() GALERA_OVERRIDE
    {
        clear_error();
        auto result(SSL_shutdown(ssl_));
        auto ssl_error __attribute__((unused)) (::SSL_get_error(ssl_, result));
        auto sys_error __attribute__((unused)) (::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::shutdown: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error);
    }

    virtual op_result read(void* buf, size_t max_count) GALERA_OVERRIDE
    {
        clear_error();
        return do_read(buf, max_count);
    }

    virtual op_result write(const void* buf, size_t count) GALERA_OVERRIDE
    {
        clear_error();
        return do_write(buf, count);
    }

    virtual gu::AsioErrorCode last_error() const GALERA_OVERRIDE
    {
        return gu::AsioErrorCode(last_error_,
                                 last_error_category_ ?
                                 *last_error_category_ :
                                 gu_asio_system_category,
                                 last_verify_error_);
    }

private:
    void clear_error()
    {
        last_error_ = 0;
        last_verify_error_ = 0;
        last_error_category_ = 0;
    }

#ifdef HAVE_READ_EX
    // Read method with SSL_read_ex which was introduced in 1.1.1.
    op_result do_read(void* buf, size_t max_count)
    {
        size_t bytes_transferred(0);
        auto result(SSL_read_ex(ssl_, buf, max_count, &bytes_transferred));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::read: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error
                      << " bytes transferred " << bytes_transferred);
        return op_result{map_status(ssl_error, sys_error, "read"),
                bytes_transferred};
    }
#else
    // Read method for OpenSSL versions pre 1.1.1.
    op_result do_read(void* buf, size_t max_count)
    {
        size_t bytes_transferred(0);
        auto result(SSL_read(ssl_, buf, max_count));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::read: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error
                      << " bytes transferred " << bytes_transferred);
        if (ssl_error == SSL_ERROR_WANT_READ &&
            (bytes_transferred = SSL_pending(ssl_)) > 0)
        {
            result = SSL_read(ssl_, buf, bytes_transferred);
            assert(static_cast<size_t>(result) == bytes_transferred);
            return op_result{map_status(ssl_error, sys_error, "read"),
                    bytes_transferred};
        }
        else if (result > 0)
        {
            bytes_transferred = result;
        }
        return op_result{map_status(ssl_error, sys_error, "read"),
                bytes_transferred};
    }
#endif // HAVE_READ_EX

#ifdef HAVE_WRITE_EX
    op_result do_write(const void* buf, size_t count)
    {
        size_t bytes_transferred(0);
        auto result(SSL_write_ex(ssl_, buf, count, &bytes_transferred));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::write: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error
                      << " bytes transferred " << bytes_transferred);
        return op_result{map_status(ssl_error, sys_error, "write"),
                bytes_transferred};

    }
#else
    op_result do_write(const void* buf, size_t count)
    {
        size_t bytes_transferred(0);
        auto result(SSL_write(ssl_, buf, count));
        auto ssl_error(::SSL_get_error(ssl_, result));
        auto sys_error(::ERR_get_error());
        GU_ASIO_DEBUG(this << " AsioSslStreamEngine::write: "
                      << result << " ssl error " << ssl_error
                      << " sys error " << sys_error
                      << " bytes transferred " << bytes_transferred);
        if (result > 0)
        {
            bytes_transferred = result;
        }
        return op_result{map_status(ssl_error, sys_error, "write"),
                bytes_transferred};

    }
#endif // HAVE_WRITE_EX
    enum op_status map_status(int ssl_error, int sys_error, const char* op)
    {
        switch (ssl_error)
        {
        case SSL_ERROR_NONE:
            return success;
        case SSL_ERROR_WANT_WRITE:
            return want_write;
        case SSL_ERROR_WANT_READ:
            return want_read;
        case SSL_ERROR_SYSCALL:
            last_error_ = sys_error;
            return (sys_error == 0 ? eof : error);
        case SSL_ERROR_SSL:
        {
            last_error_ = sys_error;
            last_error_category_ = &gu_asio_ssl_category;
            last_verify_error_ = SSL_get_verify_result(ssl_);
            return error;
        }
        }
        assert(0);
        return error;
    }

    int fd_;
    SSL* ssl_;
    int last_error_;
    int last_verify_error_;
    const gu::AsioErrorCategory* last_error_category_;
};

#endif // GALERA_HAVE_SSL

/*
 * DynamicStreamEngine is used to choose either TCP or SSL for socket communication.
 * Following condition should be true: Ts(server timeout) > Tc(client timeout).  
 *
 * Following diagrams show combinations possible with TCP/SSL/Dynamic stream engine. 
 *
 * 1. CLIENT - dynamic, SERVER - standard
 *
 *       C(d)           S(s/TCP)
 * |------|   <--TCP--   |
 * |  Tc  |              |
 * |----->|              |
 *        |              |
 *
 * 2. CLIENT - dynamic (with SSL), SERVER - standard (with SSL)
 *
 *       C(d)           S(s/TLS)
 * |------|              |
 * |  Tc  |              |
 * |----->|              |
 *        |   --SSL-->   |
 * |------|   <--SSL--   |
 * |  Tc  |              | * (A)
 * |----->|              |
 *
 * (A) Packet is received on second client timeout period, it should be SSL response packet
 *
 * 3. CLIENT - standard, SERVER - dynamic (with SSL)
 *
 *       C(s/TLS)       S(d)
 *       |   --SSL-->   |------|
 *       |              |      |
 *       |              |  Ts  |
 *       |              |      |
 *       |              |<-----|
 *       |              |
 *
 * 4. CLIENT - standard, SERVER - dynamic
 *
 *       C(s/TCP)       S(d)
 *       |              |------|
 *       |              |      |
 *       |              |  Ts  |
 *       |              |      |
 *       |              |<-----|
 *       |              |------|
 *       |              |      |
 *       |              |  Ts  |
 *       |              |      |
 *       |              |<-----|
 *       |   <--TCP--   |
 *       |              |
 *
 * 5. CLIENT - dynamic (with SSL), SERVER - dynamic (with SSL)
 *
 *       C(d)           S(d)
 * |------|              |------|
 * |  Tc  |              |      |
 * |----->|              |  Ts  |
 *        |   --SSL-->   |      | * (A)
 * |------|   <--SSL--   |<-----| * (B)
 * |  Tc  |              |
 * |----->|              |
 *
 * (A) Packet is received on first server timeout, it should be SSL request packet,
       we support SSL so we'll send SSL response
 * (B) Packet is received on second client timeout, it should be SSL response packet
 *
 * 6. CLIENT - dynamic (with SSL), SERVER - dynamic (without SSL)
 *
 *       C(d)           S(d)
 * |------|              |------|
 * |  Tc  |              |      |
 * |----->|              |  Ts  |
 *        |   --SSL-->   |      | * (A)
 * |------|              |<-----|
 * |  Tc  |              |<-----|
 * |----->|              |      |
 *        |              |  Ts  |
 *        |              |      |
 *        |              |<-----|
 *        |   <--TCP--   |
 *        |   --TCP-->   |
 *
 * (A) Packet is received on first timeout, it should be SSL request packet, but we don't
 *     support SSL so we should timeout
 * (B) Nothing is received on second timeout period, so it should be TCP packet
 *
 * 7. CLIENT - dynamic (without SSL), SERVER - dynamic (with/without SSL)
 *
 *       C(d)           S(d)
 * |------|              |------|
 * |  Tc  |              |      |
 * |----->|              |  Ts  |
 *        |              |      |
 *        |              |<-----|
 *        |              |<-----|
 *        |              |      |
 *        |              |  Ts  |
 *        |              |      |
 *        |              |<-----|
 *        |   <--TCP--   |        * (A)
 *        |   --TCP-->   |
 *
 * (A) Packet is on client received after timeout period, SSL CLIENT HELLO was not
 *     sent, so it should be TCP packet
 *
 */

class AsioDynamicStreamEngine : public gu::AsioStreamEngine
{
public:
    AsioDynamicStreamEngine(gu::AsioIoService& io_service, int fd, 
                            bool non_blocking, bool encrypted_protocol)
        : client_timeout_(500 * gu::datetime::MSec)
        , server_timeout_(750 * gu::datetime::MSec)
        , fd_(fd)
        , io_service_(io_service)
        , engine_(std::make_shared<AsioTcpStreamEngine>(fd_))
        , non_blocking_(non_blocking)
        , have_encrypted_protocol_(encrypted_protocol)
        , timer_check_done_(false)
        , client_encrypted_message_sent_(false)
        , client_encrypted_message_sent_ts_(gu::datetime::Date::zero())
    {
    }

    ~AsioDynamicStreamEngine()
    {
    }

    AsioDynamicStreamEngine(const AsioDynamicStreamEngine&) = delete;
    AsioDynamicStreamEngine& operator=(const AsioDynamicStreamEngine&) = delete;

    virtual std::string scheme() const GALERA_OVERRIDE
    {
        return engine_->scheme();
    }

    virtual enum op_status client_handshake() GALERA_OVERRIDE
    {
        if (not timer_check_done_)
        {
            if (not client_encrypted_message_sent_)
            {
                bool received = socket_poll(client_timeout_.get_nsecs() / gu::datetime::MSec);
                if (have_encrypted_protocol_ && not received)
                {
                    engine_.reset();
                    engine_ = std::make_shared<AsioSslStreamEngine>(io_service_, fd_);
                    client_encrypted_message_sent_ = true;
                    client_encrypted_message_sent_ts_ = gu::datetime::Date::monotonic();
                    if (not non_blocking_)
                    {
                        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
                    }
                    op_status result = success;
                    bool tcp_engine_switch = false;
                    while(true)
                    {
                        result = engine_->client_handshake() ;
                        if (non_blocking_) 
                        {
                            return result;
                        }
                        if (result == AsioStreamEngine::success ||
                            result == AsioStreamEngine::error)
                        {
                            break;
                        }
                        received = socket_poll(client_timeout_.get_nsecs() / gu::datetime::MSec);
                        if (not received)
                        {
                            engine_.reset();
                            engine_ = std::make_shared<AsioTcpStreamEngine>(fd_);
                            tcp_engine_switch = true;
                            break;
                        }
                    }
                    if (not non_blocking_)
                    {
                        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) ^ O_NONBLOCK);
                        if (not tcp_engine_switch)
                        {
                            return result;
                        }
                    }
                }
            }
            else
            {
                gu::datetime::Date now(gu::datetime::Date::monotonic());
                if (client_encrypted_message_sent_ts_ + client_timeout_ < now)
                {
                    engine_.reset();
                    engine_ = std::make_shared<AsioTcpStreamEngine>(fd_);
                }
            }
            timer_check_done_ = true;
        }
        return engine_->client_handshake();
    }

    virtual enum op_status server_handshake() GALERA_OVERRIDE
    {
        if (not timer_check_done_)
        {
            bool received = socket_poll(server_timeout_.get_nsecs() / gu::datetime::MSec);
            int bytes_available;
            ioctl(fd_, FIONREAD, &bytes_available);
            if (have_encrypted_protocol_ && received && bytes_available)
            {
                engine_.reset();
                engine_ = std::make_shared<AsioSslStreamEngine>(io_service_, fd_);
                timer_check_done_ = true;
                return engine_->server_handshake();
            }
            else if (not have_encrypted_protocol_)
            {
                if (received && bytes_available)
                {
                    std::vector<char> pending_data(bytes_available);
                    engine_->read(pending_data.data(), bytes_available);
                }
                socket_poll(server_timeout_.get_nsecs() / gu::datetime::MSec);
            }
            timer_check_done_ = true;
        }
        return engine_->server_handshake();
    }

    virtual void shutdown() GALERA_OVERRIDE
    {
        engine_->shutdown();
        timer_check_done_ = false;
        client_encrypted_message_sent_ = false;
        engine_ = std::make_shared<AsioTcpStreamEngine>(fd_);
    }

    virtual op_result read(void* buf, size_t max_count) GALERA_OVERRIDE
    {
        return engine_->read(buf, max_count);
    }

    virtual op_result write(const void* buf, size_t count) GALERA_OVERRIDE
    {
        return engine_->write(buf, count);
    }

    virtual gu::AsioErrorCode last_error() const GALERA_OVERRIDE
    {
        return engine_->last_error();
    }

private:
    bool socket_poll(long msec)
    {
        struct pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        switch (poll(&pfd, 1, msec))
        {
            // Timeout
            case 0:
            {
                return false;
            }
            // Error
            case -1:
            {
                return false;
            }
            // Data available
            default:
            {
                if (pfd.revents & POLLIN)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }
    }
    gu::datetime::Period client_timeout_;
    gu::datetime::Period server_timeout_;
    int fd_;
    gu::AsioIoService& io_service_;
    std::shared_ptr<AsioStreamEngine> engine_;
    bool non_blocking_;
    bool have_encrypted_protocol_;
    bool timer_check_done_;
    bool client_encrypted_message_sent_;
    gu::datetime::Date client_encrypted_message_sent_ts_;
};

std::shared_ptr<gu::AsioStreamEngine> gu::AsioStreamEngine::make(
    AsioIoService& io_service, const std::string& scheme, int fd, bool non_blocking)
{
    if (scheme == "tcp")
    {
        if (not io_service.dynamic_socket_enabled())
        {
            GU_ASIO_DEBUG("AsioStreamEngine::make use TCP engine");
            return std::make_shared<AsioTcpStreamEngine>(fd);
        }
        else
        {
            GU_ASIO_DEBUG("AsioStreamEngine::make use Dynamic engine")
            return std::make_shared<AsioDynamicStreamEngine>(io_service, fd,
                                                            non_blocking,
                                                            io_service.ssl_enabled());
        }
    }
#ifdef GALERA_HAVE_SSL
    else if (scheme == "ssl")
    {
        if (not io_service.dynamic_socket_enabled())
        {
           GU_ASIO_DEBUG("AsioStreamEngine::make use SSL engine");
           return std::make_shared<AsioSslStreamEngine>(io_service, fd);
        }
        else
        {
            GU_ASIO_DEBUG("AsioStreamEngine::make use Dynamic engine");
           return std::make_shared<AsioDynamicStreamEngine>(io_service, fd,
                                                            non_blocking,
                                                            io_service.ssl_enabled());
        }
    }
#endif // GALERA_HAVE_SSL
    else
    {
        gu_throw_error(EINVAL)
            << "Stream engine not implemented for scheme " << scheme;
        return std::shared_ptr<gu::AsioStreamEngine>();
    }
}

std::ostream& gu::operator<<(std::ostream& os,
                         enum AsioStreamEngine::op_status status)
{
    switch (status)
    {
    case AsioStreamEngine::success:
        os << "success";
        break;
    case AsioStreamEngine::want_read:
        os << "want_read";
        break;
    case AsioStreamEngine::want_write:
        os << "want_write";
        break;
    case AsioStreamEngine::eof:
        os << "eof";
        break;
    case AsioStreamEngine::error:
        os << "error";
        break;
    default:
        os << "unknown(" << static_cast<int>(status) << ")";
        break;
    }
    return os;
}
