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
                                 gu_asio_system_category);
    }

private:
    void clear_error() { last_error_ = 0; last_error_category_ = 0; }

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
            char error_str[120];
            log_error << op << " error: "
                      << ERR_error_string(sys_error, error_str);
            return error;
        }
        }
        assert(0);
        return error;
    }

    int fd_;
    SSL* ssl_;
    int last_error_;
    const gu::AsioErrorCategory* last_error_category_;
};

#endif // GALERA_HAVE_SSL

std::shared_ptr<gu::AsioStreamEngine> gu::AsioStreamEngine::make(
    AsioIoService& io_service, const std::string& scheme, int fd)
{
    if (scheme == "tcp")
    {
        GU_ASIO_DEBUG("AsioStreamEngine::make use TCP engine");
        return std::make_shared<AsioTcpStreamEngine>(fd);
    }
#ifdef GALERA_HAVE_SSL
    else if (scheme == "ssl")
    {
        GU_ASIO_DEBUG("AsioStreamEngine::make use SSL engine");
        return std::make_shared<AsioSslStreamEngine>(io_service, fd);
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
