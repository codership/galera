//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

#define GU_ASIO_IMPL

#include "gu_asio_stream_react.hpp"

#include "gu_asio_debug.hpp"
#include "gu_asio_error_category.hpp"
#include "gu_asio_io_service_impl.hpp"
#include "gu_asio_socket_util.hpp"
#include "gu_asio_utils.hpp"

#ifndef ASIO_HAS_BOOST_BIND
#define ASIO_HAS_BOOST_BIND
#endif // ASIO_HAS_BOOST_BIND
#include "asio/placeholders.hpp"
#include "asio/read.hpp"
#include "asio/write.hpp"

#include <boost/bind.hpp>


gu::AsioStreamReact::AsioStreamReact(
    AsioIoService& io_service,
    const std::string& scheme,
    const std::shared_ptr<AsioStreamEngine>& engine)
    : io_service_(io_service)
    , socket_(io_service_.impl().native())
    , scheme_(scheme)
    , engine_(engine)
    , local_addr_()
    , remote_addr_()
    , connected_()
    , non_blocking_()
    , in_progress_()
    , read_context_()
    , write_context_()
{ }

gu::AsioStreamReact::~AsioStreamReact()
{
    shutdown();
}

void gu::AsioStreamReact::open(const gu::URI& uri)
{
    try
    {
        auto resolve_result(resolve_tcp(io_service_.impl().native(), uri));
        socket_.open(resolve_result->endpoint().protocol());
        set_fd_options(socket_);
    }
    catch (const asio::system_error& e)
    {
        gu_throw_error(e.code().value())
            << "error opening stream socket " << uri;
    }
}

bool gu::AsioStreamReact::is_open() const
{
    return socket_.is_open();
}

void gu::AsioStreamReact::close() try
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::close");
    socket_.close();
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Failed to close socket: " << e.what();
}

void gu::AsioStreamReact::bind(const gu::AsioIpAddress& addr)
{
    ::bind(socket_, addr);
}

void gu::AsioStreamReact::async_connect(
    const gu::URI& uri,
    const std::shared_ptr<AsioSocketHandler>& handler)
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::connect: " << uri);
    auto resolve_result(resolve_tcp(io_service_.impl().native(), uri));
    if (not socket_.is_open())
    {
        socket_.open(resolve_result->endpoint().protocol());
    }
    connected_ = true;
    socket_.async_connect(*resolve_result,
                          boost::bind(&AsioStreamReact::connect_handler,
                                      shared_from_this(),
                                      handler,
                                      asio::placeholders::error));
}

void gu::AsioStreamReact::async_write(
    const std::array<AsioConstBuffer, 2>& bufs,
    const std::shared_ptr<AsioSocketHandler>& handler) try
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::async_write: buf pointer "
                  << "ops in progress " << in_progress_);
    if (write_context_.buf().size())
    {
        gu_throw_error(EBUSY) << "Trying to write into busy socket";
    }

    write_context_ = WriteContext(bufs);
    start_async_write(&AsioStreamReact::write_handler, handler);
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Async write failed '"
                                     << e.what();
}

void gu::AsioStreamReact::async_read(
    const AsioMutableBuffer& buf,
    const std::shared_ptr<AsioSocketHandler>& handler) try
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::async_read: buf pointer: "
                  << buf.data() << " buf size: " << buf.size());
    assert(not read_context_.buf().data());
    read_context_ = ReadContext(buf);
    start_async_read(&AsioStreamReact::read_handler, handler);
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Async read failed '"
                                     << e.what();
}

static void throw_sync_op_error(const gu::AsioStreamEngine& engine,
                                const char* prefix)
{
    auto last_error(engine.last_error());
    if (last_error.is_system())
        gu_throw_error(last_error.value()) << prefix
                                           << ": " << last_error.message();
    else
        gu_throw_error(EPROTO) << prefix
                               << ": " << last_error.message();
}


void gu::AsioStreamReact::connect(const gu::URI& uri) try
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::connect: " << uri);
    auto resolve_result(resolve_tcp(io_service_.impl().native(), uri));
    if (not socket_.is_open())
    {
        socket_.open(resolve_result->endpoint().protocol());
        set_fd_options(socket_);
    }
    socket_.connect(resolve_result->endpoint());
    connected_ = true;
    prepare_engine();
    auto result(engine_->client_handshake());
    switch (result)
    {
    case AsioStreamEngine::success:
        return;
    case AsioStreamEngine::want_read:
    case AsioStreamEngine::want_write:
    case AsioStreamEngine::eof:
        gu_throw_error(EPROTO) << "Got unexpected return from client handshake: "
                               << result;
        break;
    default:
        throw_sync_op_error(*engine_, "Client handshake failed");
    }
}
catch (asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Failed to connect '"
                                     << uri << "': " << e.what();
}

size_t gu::AsioStreamReact::write(const AsioConstBuffer& buf) try
{
    assert(buf.size() > 0);
    set_non_blocking(false);
    auto write_result(engine_->write(buf.data(), buf.size()));
    switch (write_result.status)
    {
    case AsioStreamEngine::success:
        assert(write_result.bytes_transferred == buf.size());
        return write_result.bytes_transferred;
    case AsioStreamEngine::want_read:
    case AsioStreamEngine::want_write:
    case AsioStreamEngine::eof:
        gu_throw_error(EPROTO) << "Got unexpected return from write: "
                               << write_result.status;
        return 0;
    default:
        throw_sync_op_error(*engine_, "Failed to write");
        return 0; // Keep compiler happy
    }
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Failed to write: " << e.what();
}

size_t gu::AsioStreamReact::read(const AsioMutableBuffer& buf) try
{
    set_non_blocking(false);
    size_t total_transferred(0);
    do
    {
        auto read_result(
            engine_->read(
                static_cast<unsigned char*>(buf.data()) + total_transferred,
                buf.size() - total_transferred));
        switch (read_result.status)
        {
        case AsioStreamEngine::success:
            total_transferred += read_result.bytes_transferred;
            break;
        case AsioStreamEngine::eof:
            return 0;
        case AsioStreamEngine::want_read:
        case AsioStreamEngine::want_write:
            gu_throw_error(EPROTO) << "Got unexpected return from read: "
                                   << read_result.status;
            return 0;
        default:
            throw_sync_op_error(*engine_, "Failed to read");
            return 0;
        }
    }
    while (total_transferred != buf.size());
    return total_transferred;
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value()) << "Failed to read: " << e.what();
}

std::string gu::AsioStreamReact::local_addr() const
{
    return local_addr_;
}

std::string gu::AsioStreamReact::remote_addr() const
{
    return remote_addr_;
}

void gu::AsioStreamReact::set_receive_buffer_size(size_t size)
{
    assert(not connected_);
    ::set_receive_buffer_size(socket_, size);
}

size_t gu::AsioStreamReact::get_receive_buffer_size()
{
    return ::get_receive_buffer_size(socket_);
}

void gu::AsioStreamReact::set_send_buffer_size(size_t size)
{
    assert(not connected_);
    ::set_send_buffer_size(socket_, size);
}

size_t gu::AsioStreamReact::get_send_buffer_size()
{
    return ::get_send_buffer_size(socket_);
}

struct tcp_info gu::AsioStreamReact::get_tcp_info()
{
    return ::get_tcp_info(socket_);
}


void gu::AsioStreamReact::connect_handler(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const asio::error_code& ec)
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::connect_handler: " << ec);
    if (ec)
    {
        handler->connect_handler(*this, AsioErrorCode(ec.value(), ec.category()));
        socket_.close();
        return;
    }

    assign_addresses();
    set_socket_options(socket_);
    prepare_engine();
    GU_ASIO_DEBUG(debug_print()
                  << " AsioStreamReact::connect_handler: init handshake");
    auto result(engine_->client_handshake());
    switch (result)
    {
    case AsioStreamEngine::success:
        handler->connect_handler(
            *this, AsioErrorCode(ec.value(), ec.category()));
        break;
    case AsioStreamEngine::want_read:
        start_async_read(&AsioStreamReact::client_handshake_handler, handler);
        break;
    case AsioStreamEngine::want_write:
        start_async_write(&AsioStreamReact::client_handshake_handler, handler);
        break;
    case AsioStreamEngine::eof:
        handler->connect_handler(*this,
                                 AsioErrorCode(asio::error::misc_errors::eof,
                                               gu_asio_misc_category));
        break;
    case AsioStreamEngine::error:
        handler->connect_handler(*this, engine_->last_error());
        break;
    default:
        handler->connect_handler(*this, AsioErrorCode(EPROTO));
        break;
        assert(0);
    }
}

void gu::AsioStreamReact::client_handshake_handler(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const asio::error_code& ec)
{
    // During handshake there is only read or write in progress
    // at the time. Therefore safe to clear both flags.
    in_progress_ &= ~(read_in_progress | write_in_progress);
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::client_handshake_handler: " << ec);
    if (ec)
    {
        handler->connect_handler(
            *this, AsioErrorCode(ec.value(), ec.category()));
        socket_.close();
        return;
    }
    auto result(engine_->client_handshake());
    GU_ASIO_DEBUG(debug_print()
                  << "AsioStreamReact::client_handshake_handler: result from engine: "
                  << result);
    switch (result)
    {
    case AsioStreamEngine::success:
        handler->connect_handler(
            *this, AsioErrorCode(ec.value(), ec.category()));
        break;
    case AsioStreamEngine::want_read:
        start_async_read(&AsioStreamReact::client_handshake_handler, handler);
        break;
    case AsioStreamEngine::want_write:
        start_async_write(&AsioStreamReact::client_handshake_handler, handler);
        break;
    case AsioStreamEngine::eof:
        handler->connect_handler(*this,
                                 AsioErrorCode(asio::error::misc_errors::eof,
                                               gu_asio_misc_category));
        break;
    case AsioStreamEngine::error:
        handler->connect_handler(*this, engine_->last_error());
        break;
    default:
        assert(0);
        handler->connect_handler(*this, AsioErrorCode(EPROTO));
        break;
    }
}

void gu::AsioStreamReact::server_handshake_handler(
    const std::shared_ptr<AsioAcceptor>& acceptor,
    const std::shared_ptr<AsioAcceptorHandler>& acceptor_handler,
    const asio::error_code& ec)
{
    // During handshake there is only read or write in progress
    // at the time. Therefore safe to clear both flags.
    in_progress_ &= ~(read_in_progress | write_in_progress);
    if (ec)
    {
        acceptor_handler->accept_handler(
            *acceptor, shared_from_this(),
            AsioErrorCode(ec.value(), ec.category()));
        return;
    }
    auto result(engine_->server_handshake());
    GU_ASIO_DEBUG(debug_print() << "AsioStreamReact::server_handshake_handler: "
                  << "result from engine: " << result);
    switch (result)
    {
    case AsioStreamEngine::success:
        acceptor_handler->accept_handler(*acceptor, shared_from_this(),
                                         AsioErrorCode());
        break;
    case AsioStreamEngine::want_read:
        start_async_read(&AsioStreamReact::server_handshake_handler,
                         acceptor,
                         acceptor_handler);
        break;
    case AsioStreamEngine::want_write:
        start_async_write(&AsioStreamReact::server_handshake_handler,
                          acceptor,
                          acceptor_handler);
        break;
    case AsioStreamEngine::eof:
    case AsioStreamEngine::error:
        // Restart accepting transparently. The socket will go out of
        // scope and will be destructed.
        //
        // However, note that with this way of notifying the initiator
        // of accept operation will never happen before the handshake
        // is over. This means that there may be only one socket performing
        // server side handshake at the time. To get around this, the
        // actual connect/accept events must be exposed to acceptor/connector
        // handler, forcing them to initiate handshake.
        acceptor->async_accept(acceptor_handler);
        break;
    }
}


void gu::AsioStreamReact::read_handler(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const asio::error_code& ec)
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::read_handler: " << ec);

    in_progress_ &= ~read_in_progress;
    if (in_progress_ & shutdown_in_progress) return;

    if (ec)
    {
        handle_read_handler_error(handler,
                                  AsioErrorCode(ec.value(), ec.category()));
        return;
    }

    const size_t left_to_read(read_context_.left_to_read());
    assert(left_to_read <=
           read_context_.buf().size() - read_context_.bytes_transferred());
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::read_handler: left_to_read: "
                  << left_to_read);
    auto read_result(
        engine_->read(reinterpret_cast<char*>(read_context_.buf().data())
                        + read_context_.bytes_transferred(),
                        left_to_read));
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::read_handler: bytes_read: "
                  << read_result.bytes_transferred);
    if (read_result.bytes_transferred)
    {
        complete_read_op(handler, read_result.bytes_transferred);
    }
    switch (read_result.status)
    {
    case AsioStreamEngine::success:
        // In case more reads were needed to transfer all data, the
        // read operation was started in complete_read_op().
        break;
    case AsioStreamEngine::want_read:
        GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::read_handler: "
                      << "would block/want read");
        start_async_read(&AsioStreamReact::read_handler, handler);
        break;
    case AsioStreamEngine::want_write:
        GU_ASIO_DEBUG(debug_print()
                      << " AsioStreamReact::read_handler: want write");
        start_async_write(&AsioStreamReact::read_handler, handler);
        break;
    case AsioStreamEngine::eof:
        GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::read_handler: eof");
        handle_read_handler_error(
            handler,
            AsioErrorCode(asio::error::misc_errors::eof,
                          gu_asio_misc_category));
        break;
    case AsioStreamEngine::error:
        GU_ASIO_DEBUG(debug_print()
                      << " AsioStreamReact::read_handler: Read error: "
                      << ec.message() << " status " << read_result.status);
        handle_read_handler_error(handler, engine_->last_error());
        break;
    }
}

void gu::AsioStreamReact::write_handler(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const asio::error_code& ec)
{
    GU_ASIO_DEBUG(debug_print() << " AsioStreamReact::write_handler: " << ec);
    in_progress_ &= ~write_in_progress;
    if (in_progress_ & shutdown_in_progress) return;
    if (ec)
    {
        handle_write_handler_error(handler,
                                   AsioErrorCode(ec.value(), ec.category()));
        return;
    }

    AsioStreamEngine::op_result write_result(
        engine_->write(
            write_context_.buf().data() + write_context_.bytes_transferred(),
            write_context_.buf().size() - write_context_.bytes_transferred()));

    if (write_result.bytes_transferred)
    {
        complete_write_op(handler, write_result.bytes_transferred);
    }
    switch (write_result.status)
    {
    case AsioStreamEngine::success:
        // In case more writes were needed to transfer all data, the
        // write operation was started in complete_read_op().
        break;
    case AsioStreamEngine::want_write:
        start_async_write(&AsioStreamReact::write_handler, handler);
        break;
    case AsioStreamEngine::want_read:
        start_async_read(&AsioStreamReact::write_handler, handler);
        break;
    case AsioStreamEngine::eof:
        handle_write_handler_error(
            handler,
            AsioErrorCode(asio::error::misc_errors::eof,
                          gu_asio_misc_category));
        break;
    case AsioStreamEngine::error:
        GU_ASIO_DEBUG(debug_print()
                      << " AsioStreamReact::write_handler: Write error: "
                      << engine_->last_error());
        handle_write_handler_error(handler, engine_->last_error());
        break;
    }
}


//
// Private
//

void gu::AsioStreamReact::assign_addresses()
{
    local_addr_ = ::uri_string(
        scheme_,
        ::escape_addr(socket_.local_endpoint().address()),
        gu::to_string(socket_.local_endpoint().port()));
    remote_addr_ = ::uri_string(
        scheme_,
        ::escape_addr(socket_.remote_endpoint().address()),
        gu::to_string(socket_.remote_endpoint().port()));
}

void gu::AsioStreamReact::prepare_engine()
{
    if (not engine_)
    {
        engine_ = AsioStreamEngine::make(io_service_, scheme_,
                                         native_socket_handle(socket_));
    }
    else
    {
        engine_->assign_fd(native_socket_handle(socket_));
    }
}

template <typename Fn, typename ...FnArgs>
void gu::AsioStreamReact::start_async_read(Fn fn, FnArgs... fn_args)
{
    if (in_progress_ & read_in_progress) return;
    set_non_blocking(true);
    socket_.async_read_some(asio::null_buffers(),
                            boost::bind(fn,
                                        shared_from_this(),
                                        fn_args...,
                                        asio::placeholders::error));
    in_progress_ |= read_in_progress;
}

template <typename Fn, typename ...FnArgs>
void gu::AsioStreamReact::start_async_write(Fn fn, FnArgs... fn_args)
{
    if (in_progress_ & write_in_progress) return;
    set_non_blocking(true);
    socket_.async_write_some(asio::null_buffers(),
                             boost::bind(fn,
                                         shared_from_this(),
                                         fn_args...,
                                         asio::placeholders::error));
    in_progress_ |= write_in_progress;
}

void gu::AsioStreamReact::complete_read_op(
    const std::shared_ptr<AsioSocketHandler>& handler,
    size_t bytes_transferred)
{
    assert(bytes_transferred);

    read_context_.inc_bytes_transferred(bytes_transferred);
    const size_t read_completion(
        handler->read_completion_condition(
            *this,
            AsioErrorCode(),
            read_context_.bytes_transferred()));
    if (read_completion == 0)
    {
        auto total_transferred(read_context_.bytes_transferred());
        read_context_.reset();
        handler->read_handler(*this, AsioErrorCode(), total_transferred);
    }
    else
    {
        // Refuse to read more than there is available space left
        // in read buffer.
        read_context_.read_completion(
            std::min(read_completion,
                     read_context_.buf().size()
                     - read_context_.bytes_transferred()));
        start_async_read(&AsioStreamReact::read_handler, handler);
    }
}

void gu::AsioStreamReact::complete_write_op(
    const std::shared_ptr<AsioSocketHandler>& handler,
    size_t bytes_transferred)
{
    assert(bytes_transferred);

    write_context_.inc_bytes_transferred(bytes_transferred);
    if (write_context_.bytes_transferred() == write_context_.buf().size())
    {
        auto total_transferred(write_context_.bytes_transferred());
        write_context_.reset();
        handler->write_handler(*this, AsioErrorCode(), total_transferred);
    }
    else
    {
        start_async_write(&AsioStreamReact::write_handler, handler);
    }
}


void gu::AsioStreamReact::handle_read_handler_error(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const AsioErrorCode& ec)
{
    shutdown();
    handler->read_completion_condition(
        *this,
        ec,
        read_context_.bytes_transferred());
    handler->read_handler(
        *this,
        ec,
        read_context_.bytes_transferred());
    socket_.close();
}

void gu::AsioStreamReact::handle_write_handler_error(
    const std::shared_ptr<AsioSocketHandler>& handler,
    const AsioErrorCode& ec)
{
    shutdown();
    handler->write_handler(
        *this,
        ec,
        write_context_.bytes_transferred());
    socket_.close();
}

void gu::AsioStreamReact::set_non_blocking(bool val)
{
    // Socket which is once set to non-blocking mode should never
    // be switched back to blocking. This is to detect mixed use
    // of sync and async operations, which are undefined behavior.
    assert(not non_blocking_ || val);
    if (non_blocking_ != val)
    {
        socket_.non_blocking(val);
        socket_.native_non_blocking(val);
        non_blocking_ = val;
    }
}

void gu::AsioStreamReact::shutdown()
{
    if (not (in_progress_ & shutdown_in_progress) && engine_)
    {
        engine_->shutdown();
        in_progress_ |= shutdown_in_progress;
    }
}

std::string gu::AsioStreamReact::debug_print() const
{
    std::ostringstream oss;
    oss << this << ": " << scheme_ << " l: " << local_addr_
        << " r: " << remote_addr_ << " c: " << connected_
        << " nb: " << non_blocking_ << " s: " << engine_.get();
    return oss.str();
}

//
// Acceptor
//

gu::AsioAcceptorReact::AsioAcceptorReact(AsioIoService& io_service,
                                         const std::string& scheme)
    : io_service_(io_service)
    , acceptor_(io_service_.impl().native())
    , scheme_(scheme)
    , listening_()
    , engine_()
{ }

void gu::AsioAcceptorReact::open(const gu::URI& uri)
{
    auto resolve_result(resolve_tcp(io_service_.impl().native(), uri));
    acceptor_.open(resolve_result->endpoint().protocol());
    set_fd_options(acceptor_);
}

void gu::AsioAcceptorReact::listen(const gu::URI& uri)
{
    auto resolve_result(resolve_tcp(io_service_.impl().native(), uri));
    if (not acceptor_.is_open())
    {
        acceptor_.open(resolve_result->endpoint().protocol());
        set_fd_options(acceptor_);
    }

    acceptor_.set_option(asio::ip::tcp::socket::reuse_address(true));
    acceptor_.bind(*resolve_result);
    acceptor_.listen();
    listening_ = true;
}

void gu::AsioAcceptorReact::close()
{
    if (acceptor_.is_open())
    {
        acceptor_.close();
    }
    listening_ = false;
}

void gu::AsioAcceptorReact::async_accept(
    const std::shared_ptr<AsioAcceptorHandler>& handler,
    const std::shared_ptr<AsioStreamEngine>& engine)
{
    GU_ASIO_DEBUG(this << " AsioAcceptorReact::async_accept: " << listen_addr());
    auto new_socket(std::make_shared<AsioStreamReact>(
                        io_service_, scheme_, engine));
    acceptor_.async_accept(new_socket->socket_,
                           boost::bind(&AsioAcceptorReact::accept_handler,
                                       shared_from_this(),
                                       new_socket,
                                       handler,
                                       asio::placeholders::error));

}

std::shared_ptr<gu::AsioSocket> gu::AsioAcceptorReact::accept()
{
    auto socket(std::make_shared<AsioStreamReact>(io_service_, scheme_,
                                                  nullptr));
    acceptor_.accept(socket->socket_);
    socket->assign_addresses();
    set_socket_options(socket->socket_);
    socket->prepare_engine();
    auto result(socket->engine_->server_handshake());
    switch (result)
    {
    case AsioStreamEngine::success:
        return socket;
    case AsioStreamEngine::want_read:
    case AsioStreamEngine::want_write:
    case AsioStreamEngine::eof:
        gu_throw_error(EPROTO) << "Got unexpected return from server handshake: "
                               << result;
        return std::shared_ptr<gu::AsioSocket>();
    case AsioStreamEngine::error:
        throw_sync_op_error(*socket->engine_, "Server handshake failed");
        return std::shared_ptr<gu::AsioSocket>(); // Keep compiler happy
    }
    return socket;
}

std::string gu::AsioAcceptorReact::listen_addr() const try
{
    return uri_string(
        scheme_,
        ::escape_addr(acceptor_.local_endpoint().address()),
        gu::to_string(acceptor_.local_endpoint().port()));
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value())
        << "failed to read listen addr "
        << "', asio error '" << e.what() << "'";
}

unsigned short gu::AsioAcceptorReact::listen_port() const try
{
    return acceptor_.local_endpoint().port();
}
catch (const asio::system_error& e)
{
    gu_throw_error(e.code().value())
        << "failed to read listen port "
        << "', asio error '" << e.what() << "'";
}

void gu::AsioAcceptorReact::set_receive_buffer_size(size_t size)
{
    assert(not listening_);
    ::set_receive_buffer_size(acceptor_, size);
}

size_t gu::AsioAcceptorReact::get_receive_buffer_size()
{
    return ::get_receive_buffer_size(acceptor_);
}

void gu::AsioAcceptorReact::set_send_buffer_size(size_t size)
{
    assert(not listening_);
    ::set_send_buffer_size(acceptor_, size);
}

size_t gu::AsioAcceptorReact::get_send_buffer_size()
{
    return ::get_send_buffer_size(acceptor_);
}

void gu::AsioAcceptorReact::accept_handler(
    const std::shared_ptr<AsioStreamReact>& socket,
    const std::shared_ptr<AsioAcceptorHandler>& handler,
    const asio::error_code& ec)
{
    GU_ASIO_DEBUG(this << " AsioAcceptorReact::accept_handler(): " << ec);
    if (ec)
    {
        handler->accept_handler(
            *this, socket, AsioErrorCode(ec.value(), ec.category()));
        return;
    }

    socket->assign_addresses();
    set_socket_options(socket->socket_);
    socket->set_non_blocking(true);
    socket->connected_ = true;
    socket->prepare_engine();
    auto result(socket->engine_->server_handshake());
    switch (result)
    {
    case AsioStreamEngine::success:
        handler->accept_handler(*this, socket, AsioErrorCode());
        break;
    case AsioStreamEngine::want_read:
        socket->start_async_read(&AsioStreamReact::server_handshake_handler,
                                 shared_from_this(),
                                 handler);
        break;
    case AsioStreamEngine::want_write:
        socket->start_async_write(&AsioStreamReact::server_handshake_handler,
                                  shared_from_this(),
                                  handler);
        break;
    default:
        // Continue accepting transparently if socket handshake fails.
        // From user handler point of view this connection never existed
        // and it will go out of scope when this handler returns.
        async_accept(handler);
        break;
    }
}
