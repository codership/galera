/*
 * Copyright (C) 2019-2020 Codership Oy <info@codership.com>
 */


#include "gu_asio.hpp"
#define GU_ASIO_IMPL
#include "gu_asio_stream_engine.hpp"
#include "gu_asio_test.hpp"
#include "gu_buffer.hpp"
#include "gu_compiler.hpp"

#include <iterator>

//
// Helper classes
//
class MockSocketHandler : public gu::AsioSocketHandler
{
public:
    MockSocketHandler()
        : gu::AsioSocketHandler()
        , write_buffer_()
        , read_buffer_()
        , invocations_()
        , connect_handler_called_()
        , expect_read_()
        , bytes_read_()
        , bytes_written_()
        , last_error_code_()
    { }

    ~MockSocketHandler()
    {
        log_info << "~MockSocketHandler()";
    }
    virtual void connect_handler(gu::AsioSocket& socket,
                                 const gu::AsioErrorCode& ec) GALERA_OVERRIDE
    {
        log_info << "connected: " << &socket;
        invocations_.push_back("connect");
        connect_handler_called_ = true;
        last_error_code_ = ec;
    }

    virtual void write_handler(gu::AsioSocket&, const gu::AsioErrorCode& ec,
                               size_t bytes_transferred) GALERA_OVERRIDE
    {
        std::ostringstream oss;
        oss << "write:" << bytes_transferred;
        invocations_.push_back(oss.str());
        bytes_written_ += bytes_transferred;
        last_error_code_ = ec;
    }

    virtual size_t read_completion_condition(gu::AsioSocket&,
                                             const gu::AsioErrorCode& ec,
                                             size_t bytes_transferred) GALERA_OVERRIDE
    {
        std::ostringstream oss;
        oss << "read_completion:" << bytes_transferred;
        invocations_.push_back(oss.str());
        last_error_code_ = ec;
        return (expect_read_ - std::min(bytes_transferred + bytes_read_,
                                        expect_read_));
    }

    virtual void read_handler(gu::AsioSocket&, const gu::AsioErrorCode& ec,
                              size_t bytes_transferred) GALERA_OVERRIDE
    {
        std::ostringstream oss;
        oss << "read:" << bytes_transferred;
        invocations_.push_back(oss.str());
        bytes_read_ += bytes_transferred;
        last_error_code_ = ec;
        oss.str("");
        oss.clear();
        std::copy(invocations_.begin(), invocations_.end(),
                  std::ostream_iterator<std::string>(oss, "\n"));
        log_info << "Invocations so far:\n" << oss.str();
    }

    bool connect_handler_called() const { return connect_handler_called_; }
    void expect_read(size_t bytes) { expect_read_ = bytes; }
    size_t bytes_read() const { return bytes_read_; }
    void consume(size_t count)
    {
        assert(count <= bytes_read_);
        bytes_read_ -= count;
    }
    size_t bytes_written() const { return bytes_written_; }
    const gu::AsioErrorCode& last_error_code() const { return last_error_code_; }
private:
    std::array<std::string, 2> write_buffer_;
    std::string read_buffer_;
    std::vector<std::string> invocations_;
    bool connect_handler_called_;
    size_t expect_read_;
    size_t bytes_read_;
    size_t bytes_written_;
    gu::AsioErrorCode last_error_code_;
};

#include "gu_disable_non_virtual_dtor.hpp"

class MockAcceptorHandler : public gu::AsioAcceptorHandler
                          , public std::enable_shared_from_this<MockAcceptorHandler>
{
public:
    MockAcceptorHandler()
        : accepted_socket_()
        , accepted_handler_()
    { }

    ~MockAcceptorHandler()
    { }

    virtual void accept_handler(gu::AsioAcceptor& acceptor,
                                const std::shared_ptr<gu::AsioSocket>& socket,
                                const gu::AsioErrorCode& ec) GALERA_OVERRIDE
    {
        log_info << "accepted " << socket.get();
        accepted_socket_ = socket;
        accepted_handler_ = std::make_shared<MockSocketHandler>();
        // For some reason progress halts if acceptor does not keep
        // accepting.
        acceptor.async_accept(shared_from_this());
    }

    std::shared_ptr<gu::AsioSocket> accepted_socket() const
    {
        return accepted_socket_;
    }
    std::shared_ptr<MockSocketHandler> accepted_handler() const
    {
        return accepted_handler_;
    }

    void reset()
    {
        accepted_socket_.reset();
        accepted_handler_.reset();
    }
private:
    std::shared_ptr<gu::AsioSocket> accepted_socket_;
    std::shared_ptr<MockSocketHandler> accepted_handler_;
};

#include "gu_enable_non_virtual_dtor.hpp"

//
// Address
//

START_TEST(test_make_address_v4)
{
    auto a(gu::make_address("10.2.14.1"));
    ck_assert(a.is_v4());
    ck_assert(a.is_v6() == false);
}
END_TEST

// Verify that link local address without scope ID is parsed
// properly.
START_TEST(test_make_address_v6_link_local)
{
    auto a(gu::make_address("fe80::fc87:f2ff:fe85:6ba6"));
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 0);
    ck_assert(a.to_v6().is_link_local());

    a = gu::make_address("[fe80::fc87:f2ff:fe85:6ba6]");
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 0);
    ck_assert(a.to_v6().is_link_local());
}
END_TEST

// Verify that link local address with scope ID is parsed
// properly.
START_TEST(test_make_address_v6_link_local_with_scope_id)
{
    auto a(gu::make_address("fe80::fc87:f2ff:fe85:6ba6%1"));
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 1);

    a = gu::make_address("[fe80::fc87:f2ff:fe85:6ba6%1]");
    ck_assert(a.is_v4() == false);
    ck_assert(a.is_v6());
    ck_assert(a.to_v6().scope_id() == 1);
}
END_TEST

START_TEST(test_const_buffer)
{
    const char* hdr = "hdr";
    const char* data = "data";
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    ck_assert(cbs[0].size() == 3);
    ck_assert(cbs[1].size() == 4);
}
END_TEST

START_TEST(test_error_code_success)
{
    gu::AsioErrorCode ec(gu::AsioErrorCode(0));
    ck_assert(not ec);
}
END_TEST

START_TEST(test_error_code_error)
{
    gu::AsioErrorCode ec(gu::AsioErrorCode(1));
    ck_assert(ec);
}
END_TEST

START_TEST(test_io_service)
{
    gu::AsioIoService io_service;
}
END_TEST


START_TEST(test_tcp_socket)
{
    gu::AsioIoService io_service;
    auto socket(io_service.make_socket(gu::URI("tcp://127.0.0.1:0")));
}
END_TEST

template <class Socket>
void test_socket_receive_buffer_size_unopened_common(Socket& socket)
{
    try
    {
        (void)socket.get_receive_buffer_size();
        ck_abort_msg("Exception not thrown when calling get receive buffer "
                     "for closed socket");
    }
    catch (const gu::Exception&) { }
    try
    {
        socket.set_receive_buffer_size(1 << 16);
        ck_abort_msg("Exception not thrown when calling get receive buffer "
                     "for closed socket");
    }
    catch (const gu::Exception&) { }
}

START_TEST(test_tcp_socket_receive_buffer_size_unopened)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_receive_buffer_size_unopened_common(*socket);
}
END_TEST

template <class Socket>
void test_socket_receive_buffer_size_common(Socket& socket, const gu::URI& uri)
{
    socket.open(uri);
    auto default_size(socket.get_receive_buffer_size());
    socket.set_receive_buffer_size(default_size/2);
    ck_assert(socket.get_receive_buffer_size() == default_size/2);

}

START_TEST(test_tcp_socket_receive_buffer_size)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_receive_buffer_size_common(*socket, uri);
}
END_TEST

template <class Socket>
void test_socket_send_buffer_size_unopened_common(Socket& socket)
{
    try
    {
        (void)socket.get_send_buffer_size();
        ck_abort_msg("Exception not thrown when calling get send buffer "
                     "for closed socket");
    }
    catch (const gu::Exception&) { }
    try
    {
        socket.set_send_buffer_size(1 << 16);
        ck_abort_msg("Exception not thrown when calling get send buffer "
                     "for closed socket");
    }
    catch (const gu::Exception&) { }
}

START_TEST(test_tcp_socket_send_buffer_size_unopened)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_send_buffer_size_unopened_common(*socket);
}
END_TEST

template <class Socket>
void test_socket_send_buffer_size_common(Socket& socket, const gu::URI& uri)
{
    socket.open(uri);
    auto default_size(socket.get_send_buffer_size());
    socket.set_send_buffer_size(default_size/2);
    ck_assert(socket.get_send_buffer_size() == default_size/2);
}

START_TEST(test_tcp_socket_send_buffer_size)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_send_buffer_size_common(*socket, uri);
}
END_TEST

START_TEST(test_tcp_read_unopened)
{
    gu::AsioIoService io_service;
    auto socket(io_service.make_socket(gu::URI("tcp://")));
    auto socket_handler(std::make_shared<MockSocketHandler>());
    try
    {
        char b;
        gu::AsioMutableBuffer mb(&b, 1);
        socket->async_read(mb, socket_handler);
        ck_abort_msg("Exception not thrown");
    }
    catch (const gu::Exception&)
    { }
}
END_TEST

START_TEST(test_tcp_write_unopened)
{
    gu::AsioIoService io_service;
    auto socket(io_service.make_socket(gu::URI("tcp://")));
    auto socket_handler(std::make_shared<MockSocketHandler>());
    try
    {
        std::array<gu::AsioConstBuffer, 2> cbs;
        cbs[0] = gu::AsioConstBuffer("1", 1);
        cbs[1] = gu::AsioConstBuffer();
        socket->async_write(cbs, socket_handler);
        ck_abort_msg("Exception not thrown");
    }
    catch (const gu::Exception&)
    { }
}
END_TEST

START_TEST(test_tcp_acceptor)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
}
END_TEST

START_TEST(test_tcp_acceptor_listen)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    auto listen_addr(acceptor->listen_addr());
    ck_assert(listen_addr.find("tcp://127.0.0.1") != std::string::npos);
}
END_TEST

START_TEST(test_tcp_acceptor_receive_buffer_size_unopened)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
    try
    {
        (void)acceptor->get_receive_buffer_size();
        ck_abort_msg("Exception not thrown when calling get receive buffer "
                     "for closed acceptor");
    }
    catch (const gu::Exception&) { }
    try
    {
        acceptor->set_receive_buffer_size(1 << 16);
        ck_abort_msg("Exception not thrown when calling get receive buffer "
                     "for closed acceptor");
    }
    catch (const gu::Exception&) { }
}
END_TEST

START_TEST(test_tcp_acceptor_receive_buffer_size)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->open(uri);
    auto default_size(acceptor->get_receive_buffer_size());
    acceptor->set_receive_buffer_size(default_size/2);
    ck_assert(acceptor->get_receive_buffer_size() == default_size/2);
}
END_TEST

START_TEST(test_tcp_acceptor_send_buffer_size_unopened)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
    try
    {
        (void)acceptor->get_send_buffer_size();
        ck_abort_msg("Exception not thrown when calling get send buffer "
                     "for closed acceptor");
    }
    catch (const gu::Exception&) { }
    try
    {
        acceptor->set_send_buffer_size(1 << 16);
        ck_abort_msg("Exception not thrown when calling get send buffer "
                     "for closed acceptor");
    }
    catch (const gu::Exception&) { }
}
END_TEST

START_TEST(test_tcp_acceptor_send_buffer_size)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->open(uri);
    auto default_size(acceptor->get_send_buffer_size());
    acceptor->set_send_buffer_size(default_size/2);
    ck_assert(acceptor->get_send_buffer_size() == default_size/2);
}
END_TEST

template <class Acceptor>
void test_connect_common(gu::AsioIoService& io_service,
                         Acceptor& acceptor,
                         MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }

    auto accepted_socket(acceptor_handler.accepted_socket());
    ck_assert_msg(acceptor.listen_addr() == accepted_socket->local_addr(),
                "%s != %s", acceptor.listen_addr().c_str(),
                accepted_socket->local_addr().c_str());
    ck_assert(socket->local_addr() == accepted_socket->remote_addr());
    ck_assert(socket->remote_addr() == accepted_socket->local_addr());
}

START_TEST(test_tcp_connect)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_tcp_connect_twice)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
    acceptor_handler->reset();
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

template <class Acceptor>
void test_async_read_write_common(gu::AsioIoService& io_service,
                                  Acceptor& acceptor,
                                  MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }

    const char* hdr = "hdr";
    const char* data = "data";
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    socket->async_write(cbs, handler);
    while (handler->bytes_written() != strlen(hdr) + strlen(data))
    {
        io_service.run_one();
    }
    auto accepted_socket(acceptor_handler.accepted_socket());
    auto accepted_socket_handler(acceptor_handler.accepted_handler());
    char read_buf[7] = {0};
    accepted_socket_handler->expect_read(sizeof(read_buf));
    accepted_socket->async_read(gu::AsioMutableBuffer(
                                    read_buf, sizeof(read_buf)),
                                accepted_socket_handler);

    while (accepted_socket_handler->bytes_read() != strlen(hdr) + strlen(data))
    {
        io_service.run_one();
    }
    ck_assert(strncmp(read_buf, "hdrdata", sizeof(read_buf)) == 0);
}

START_TEST(test_tcp_async_read_write)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

template <class Acceptor>
void test_async_read_write_large_common(gu::AsioIoService& io_service,
                                        Acceptor& acceptor,
                                        MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }

    const char* hdr("hdr");
    gu::Buffer data(1 << 23);
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data.data(), data.size());
    socket->async_write(cbs, handler);
    auto accepted_socket(acceptor_handler.accepted_socket());
    auto accepted_socket_handler(acceptor_handler.accepted_handler());
    gu::Buffer read_buf(3 + data.size());
    accepted_socket_handler->expect_read(read_buf.size());
    accepted_socket->async_read(gu::AsioMutableBuffer(
                                    &read_buf[0], read_buf.size()),
                                accepted_socket_handler);

    while (handler->bytes_written() != 3 + data.size() &&
           accepted_socket_handler->bytes_read() != read_buf.size())
    {
        io_service.run_one();
    }
}

START_TEST(test_tcp_async_read_write_large)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_large_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

template <class Acceptor>
void test_async_read_write_small_large_common(gu::AsioIoService& io_service,
                                              Acceptor& acceptor,
                                              MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    mark_point();
    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }

    const char* hdr("hdr");
    gu::Buffer data(10);
    const size_t small_message_size(3 + data.size());
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data.data(), data.size());
    socket->async_write(cbs, handler);
    mark_point();
    size_t tot_bytes_written(small_message_size);
    while (handler->bytes_written() != tot_bytes_written)
    {
        io_service.run_one();
    }

    data.resize(1 << 16);
    const size_t large_message_size(3 + data.size());
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data.data(), data.size());
    socket->async_write(cbs, handler);
    mark_point();
    tot_bytes_written += large_message_size;
    while (handler->bytes_written() != tot_bytes_written)
    {
        io_service.run_one();
    }

    auto accepted_socket(acceptor_handler.accepted_socket());
    auto accepted_socket_handler(acceptor_handler.accepted_handler());
    // Read buffer with size to hold one message at the time. This will
    // cause partial read to happen and async_read() needs to be called
    // twice to transfer all.
    gu::Buffer read_buf(large_message_size);
    accepted_socket_handler->expect_read(small_message_size);
    accepted_socket->async_read(gu::AsioMutableBuffer(
                                    &read_buf[0], read_buf.size()),
                                accepted_socket_handler);
    mark_point();
    while (accepted_socket_handler->bytes_read() < small_message_size)
    {
        io_service.run_one();
    }
    ck_assert(::memcmp(read_buf.data(), "hdr", 3) == 0);
    // Consume the first message from the buffer and restart read.
    memmove(&read_buf[0], &read_buf[0] + small_message_size,
            accepted_socket_handler->bytes_read() - small_message_size);

    accepted_socket_handler->consume(small_message_size);
    accepted_socket_handler->expect_read(large_message_size);
    accepted_socket->async_read(
        gu::AsioMutableBuffer(
            &read_buf[0] + accepted_socket_handler->bytes_read(),
            read_buf.size() - accepted_socket_handler->bytes_read()),
        accepted_socket_handler);
    mark_point();
    while (accepted_socket_handler->bytes_read() != large_message_size)
    {
        io_service.run_one();
    }
    assert(::memcmp(read_buf.data(), "hdr", 3) == 0);
}

START_TEST(test_tcp_async_read_write_small_large)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_small_large_common(
        io_service, *acceptor, *acceptor_handler);
}
END_TEST

static void test_async_read_from_client_write_from_server_common(
    gu::AsioIoService& io_service,
    gu::AsioAcceptor& acceptor,
    MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }

    const char* hdr = "hdr";
    const char* data = "data";
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    auto accepted_socket(acceptor_handler.accepted_socket());
    auto accepted_socket_handler(acceptor_handler.accepted_handler());
    accepted_socket->async_write(cbs, accepted_socket_handler);
    while (accepted_socket_handler->bytes_written() !=
           strlen(hdr) + strlen(data))
    {
        io_service.run_one();
    }
    char read_buf[7] = {0};
    handler->expect_read(sizeof(read_buf));
    socket->async_read(gu::AsioMutableBuffer(read_buf, sizeof(read_buf)),
                       handler);

    while (handler->bytes_read() != strlen(hdr) + strlen(data))
    {
        io_service.run_one();
    }
    ck_assert(strncmp(read_buf, "hdrdata", sizeof(read_buf)) == 0);
}

START_TEST(test_tcp_async_read_from_client_write_from_server)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_from_client_write_from_server_common(
        io_service, *acceptor, *acceptor_handler);
}
END_TEST

template <class Acceptor>
void test_write_twice_wo_handling_common(gu::AsioIoService& io_service,
                                         Acceptor& acceptor,
                                         MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }
    const char* hdr = "hdr";
    const char* data = "data";

    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    socket->async_write(cbs, handler);
    try
    {
        socket->async_write(cbs, handler);
        ck_abort_msg("Exception not thrown");
    }
    catch (const gu::Exception& e)
    {
        ck_assert(e.get_errno() == EBUSY);
    }
}

// Verify that trying to write twice without waiting for
// write handler to be called will throw error.
START_TEST(test_tcp_write_twice_wo_handling)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_write_twice_wo_handling_common(io_service, *acceptor,
                                        *acceptor_handler);
}
END_TEST

void test_close_client_common(gu::AsioIoService& io_service,
                              gu::AsioAcceptor& acceptor,
                              MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }
    socket->close();

    char readbuf[1];
    acceptor_handler.accepted_socket()->async_read(
        gu::AsioMutableBuffer(readbuf, 1),
        acceptor_handler.accepted_handler());
    // Wait until socket closes.
    while (not acceptor_handler.accepted_handler()->last_error_code())
    {
        io_service.run_one();
    }
}

START_TEST(test_tcp_close_client)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_close_client_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

void test_close_server_common(gu::AsioIoService& io_service,
                              gu::AsioAcceptor& acceptor,
                              MockAcceptorHandler& acceptor_handler)
{
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }
    acceptor_handler.accepted_socket()->close();

    char readbuf[1];
    socket->async_read(gu::AsioMutableBuffer(readbuf, 1), handler);
    // Wait until socket closes.
    while (not handler->last_error_code())
    {
        io_service.run_one();
    }
}

START_TEST(test_tcp_close_server)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_close_server_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

template <class Acceptor>
void test_get_tcp_info_common(gu::AsioIoService& io_service,
                              Acceptor& acceptor,
                              MockAcceptorHandler& acceptor_handler)
{
    // Make first socket connected
    auto handler(std::make_shared<MockSocketHandler>());
    auto socket(io_service.make_socket(acceptor.listen_addr()));
    socket->async_connect(acceptor.listen_addr(), handler);

    while (not (acceptor_handler.accepted_socket() &&
                handler->connect_handler_called()))
    {
        io_service.run_one();
    }
    (void)socket->get_tcp_info();
}

START_TEST(test_tcp_get_tcp_info)
{
    gu::AsioIoService io_service;
    gu::URI uri("tcp://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_get_tcp_info_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

#ifdef GALERA_HAVE_SSL

#include <signal.h>

//
// SSL
//

static std::string get_cert_dir()
{
    // This will be set by CMake/preprocessor.
    return GU_ASIO_TEST_CERT_DIR;
}

static gu::Config get_ssl_config()
{
    gu::Config ret;
    gu::ssl_register_params(ret);
    std::string cert_dir(get_cert_dir());
    ret.set(gu::conf::use_ssl, "1");
    ret.set(gu::conf::ssl_key, cert_dir + "/galera_key.pem");
    ret.set(gu::conf::ssl_cert, cert_dir + "/galera_cert.pem");
    ret.set(gu::conf::ssl_ca, cert_dir + "/galera_cert.pem");
    gu::ssl_init_options(ret);

    // Block SIGPIPE in SSL tests. OpenSSL calls may cause
    // signal to be generated.
    struct sigaction sa;
    ::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, 0);
    return ret;
}

START_TEST(test_ssl_io_service)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
}
END_TEST

START_TEST(test_ssl_socket)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
    gu::URI uri("ssl://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
}
END_TEST

START_TEST(test_ssl_socket_receive_buffer_unopened)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
    gu::URI uri("ssl://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_receive_buffer_size_unopened_common(*socket);
}
END_TEST

START_TEST(test_ssl_socket_receive_buffer_size)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
    gu::URI uri("ssl://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_receive_buffer_size_common(*socket, uri);
}
END_TEST

START_TEST(test_ssl_socket_send_buffer_unopened)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
    gu::URI uri("ssl://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_send_buffer_size_unopened_common(*socket);
}
END_TEST

START_TEST(test_ssl_socket_send_buffer_size)
{
    auto conf(get_ssl_config());
    gu::AsioIoService io_service(conf);
    gu::URI uri("ssl://127.0.0.1:0");
    auto socket(io_service.make_socket(uri));
    test_socket_send_buffer_size_common(*socket, uri);
}
END_TEST

START_TEST(test_ssl_acceptor)
{
    gu::AsioIoService io_service;
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor(io_service.make_acceptor(uri));
}
END_TEST

START_TEST(test_ssl_connect)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_connect_twice)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
    acceptor_handler->reset();
    acceptor->async_accept(acceptor_handler);
    test_connect_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_async_read_write)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_async_read_write_large)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_large_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_async_read_write_small_large)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_small_large_common(
        io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_async_read_from_client_write_from_server)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_from_client_write_from_server_common(
        io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_write_twice_wo_handling)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_write_twice_wo_handling_common(io_service, *acceptor,
                                        *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_close_client)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_close_client_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_close_server)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_close_server_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_get_tcp_info)
{
    gu::AsioIoService io_service(get_ssl_config());
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_get_tcp_info_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

START_TEST(test_ssl_compression_option)
{
    auto config(get_ssl_config());
    config.set("socket.ssl_compression", true);
    gu::AsioIoService io_service(config);
    gu::URI uri("ssl://127.0.0.1:0");
    auto acceptor_handler(std::make_shared<MockAcceptorHandler>());
    auto acceptor(io_service.make_acceptor(uri));
    acceptor->listen(uri);
    acceptor->async_accept(acceptor_handler);
    test_async_read_write_common(io_service, *acceptor, *acceptor_handler);
}
END_TEST

#endif // GALERA_HAVE_SSL

//
// Wsrep TLS service.
//

class MockStreamEngine : public gu::AsioStreamEngine
{
public:
    MockStreamEngine();

    void assign_fd(int fd) GALERA_OVERRIDE
    {
        fd_ = fd;
    }

    enum op_status client_handshake() GALERA_OVERRIDE
    {
        ++count_client_handshake_called;
        last_error_ = next_error;
        return next_result;
    }

    enum op_status server_handshake() GALERA_OVERRIDE
    {
        log_info << "MockWsrepTlsService::server_handshake";
        ++count_server_handshake_called;
        last_error_ = next_error;
        return next_result;
    }

    op_result read(void* buf, size_t max_count) GALERA_OVERRIDE
    {
        ++count_read_called;
        ssize_t read_result(::recv(fd_, buf, max_count, 0));
        return map_return_value(read_result, want_read);
    }

    op_result write(const void* buf, size_t count) GALERA_OVERRIDE
    {
        ++count_write_called;
        ssize_t write_result(::send(fd_, buf, count, MSG_NOSIGNAL));
        return map_return_value(write_result, want_write);
    }

    void shutdown() GALERA_OVERRIDE { }

    gu::AsioErrorCode last_error() const GALERA_OVERRIDE
    {
        return last_error_;
    }

    op_result map_return_value(ssize_t result,
                               enum op_status return_on_block)
    {
        if (next_result != success)
        {
            last_error_ = next_error;
            return {next_result, size_t(result)};
        }

        if (result > 0)
        {
            return {success, size_t(result)};
        }
        else if (result == 0)
        {
            return {eof, size_t(result)};
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            last_error_ = errno;
            return {return_on_block, size_t(result)};
        }
        else
        {
            last_error_ = next_error;
            return {error, size_t(result)};
        }
    }

    enum op_status next_result;
    int next_error;
    size_t count_client_handshake_called;
    size_t count_server_handshake_called;
    size_t count_read_called;
    size_t count_write_called;

private:
    int fd_;
    int last_error_;
};

MockStreamEngine::MockStreamEngine()
    : next_result(success)
    , next_error()
    , count_client_handshake_called()
    , count_server_handshake_called()
    , count_read_called()
    , count_write_called()
    , fd_()
    , last_error_()
{ }

struct TlsServiceClientTestFixture
{
    gu::AsioIoService server_io_service;
    std::shared_ptr<MockStreamEngine> client_engine;
    gu::AsioIoService client_io_service;
    gu::URI uri;
    std::shared_ptr<gu::AsioAcceptor> acceptor;
    std::shared_ptr<MockAcceptorHandler> acceptor_handler;
    std::shared_ptr<gu::AsioSocket> socket;
    std::shared_ptr<MockSocketHandler> socket_handler;
    TlsServiceClientTestFixture()
        : server_io_service()
        , client_engine(std::make_shared<MockStreamEngine>())
        , client_io_service(gu::Config())
        , uri("tcp://127.0.0.1:0")
        , acceptor(server_io_service.make_acceptor(uri))
        , acceptor_handler(std::make_shared<MockAcceptorHandler>())
        , socket(client_io_service.make_socket(uri, client_engine))
        , socket_handler(std::make_shared<MockSocketHandler>())
    {
        acceptor->listen(uri);
        acceptor->async_accept(acceptor_handler);
        socket->async_connect(acceptor->listen_addr(), socket_handler);
        while (not acceptor_handler->accepted_socket())
        {
            server_io_service.run_one();
        }
    }
};

START_TEST(test_client_handshake_want_read)
{
    TlsServiceClientTestFixture f;
    f.client_engine->next_result = gu::AsioStreamEngine::want_read;
    // Write to accepted socket to make connected socket readable
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("serv", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.acceptor_handler->accepted_socket()->async_write(
        cbs,
        f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    while (f.client_engine->count_client_handshake_called < 2)
    {
        f.client_io_service.run_one();
    }
}
END_TEST

START_TEST(test_client_handshake_want_write)
{
    TlsServiceClientTestFixture f;
    f.client_engine->next_result = gu::AsioStreamEngine::want_write;
    while (f.client_engine->count_client_handshake_called < 2)
    {
        f.client_io_service.run_one();
    }
}
END_TEST

START_TEST(test_client_handshake_eof)
{
    TlsServiceClientTestFixture f;
    f.client_engine->next_result = gu::AsioStreamEngine::eof;
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->connect_handler_called());
    ck_assert(f.socket_handler->last_error_code().is_eof());
    ck_assert(f.client_engine->count_client_handshake_called == 1);
}
END_TEST

START_TEST(test_client_handshake_eof2)
{
    TlsServiceClientTestFixture f;
    // First op causes connect handler to restart client handshake
    // call. The EOF will now returned in client handshake handler.
    f.client_engine->next_result = gu::AsioStreamEngine::want_write;
    f.client_io_service.run_one();
    f.client_engine->next_result = gu::AsioStreamEngine::eof;
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->connect_handler_called());
    ck_assert(f.socket_handler->last_error_code().is_eof());
    ck_assert(f.client_engine->count_client_handshake_called == 2);
}
END_TEST

START_TEST(test_client_handshake_error)
{
    TlsServiceClientTestFixture f;
    f.client_engine->next_result = gu::AsioStreamEngine::error;
    f.client_engine->next_error = EPIPE;
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->connect_handler_called());
    ck_assert(f.socket_handler->last_error_code().value() == EPIPE);
    ck_assert(f.client_engine->count_client_handshake_called == 1);
}
END_TEST

START_TEST(test_client_handshake_error2)
{
    TlsServiceClientTestFixture f;
    // First op causes connect handler to restart client handshake
    // call. The error will now returned in client handshake handler.
    f.client_engine->next_result = gu::AsioStreamEngine::want_write;
    f.client_io_service.run_one();
    f.client_engine->next_result = gu::AsioStreamEngine::error;
    f.client_engine->next_error = EPIPE;
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->connect_handler_called());
    ck_assert(f.socket_handler->last_error_code().value() == EPIPE);
    ck_assert(f.client_engine->count_client_handshake_called == 2);
}
END_TEST

struct TlsServiceServerTestFixture
{
    std::shared_ptr<MockStreamEngine> server_engine;
    gu::AsioIoService server_io_service;
    gu::AsioIoService client_io_service;
    gu::URI uri;
    std::shared_ptr<gu::AsioAcceptor> acceptor;
    std::shared_ptr<MockAcceptorHandler> acceptor_handler;
    std::shared_ptr<gu::AsioSocket> socket;
    std::shared_ptr<MockSocketHandler> socket_handler;
    TlsServiceServerTestFixture()
        : server_engine(std::make_shared<MockStreamEngine>())
        , server_io_service(gu::Config())
        , client_io_service()
        , uri("tcp://127.0.0.1:0")
        , acceptor(server_io_service.make_acceptor(uri))
        , acceptor_handler(std::make_shared<MockAcceptorHandler>())
        , socket(client_io_service.make_socket(uri))
        , socket_handler(std::make_shared<MockSocketHandler>())
    {
        acceptor->listen(uri);
        acceptor->async_accept(acceptor_handler, server_engine);
        socket->async_connect(acceptor->listen_addr(), socket_handler);
        client_io_service.run_one();
        // client_io_service runs out of work. Reset to make
        // followig calls succeed
        client_io_service.reset();
    }
};

START_TEST(test_server_handshake_want_read)
{
    TlsServiceServerTestFixture f;
    f.server_engine->next_result = gu::AsioStreamEngine::want_read;
    while (f.server_engine->count_server_handshake_called < 1)
    {
        f.server_io_service.run_one();
    }
    ck_assert(f.server_engine->count_server_handshake_called == 1);
    // Write to connected socket to make accepted socket readable
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("clie", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.socket->async_write(cbs, f.socket_handler);
    while (f.socket_handler->bytes_written() < 4)
    {
        f.client_io_service.run_one();
    }
    while (f.server_engine->count_server_handshake_called < 2)
    {
        f.server_io_service.run_one();
    }
}
END_TEST

START_TEST(test_server_handshake_want_write)
{
    TlsServiceServerTestFixture f;
    f.server_engine->next_result = gu::AsioStreamEngine::want_write;
    while (f.server_engine->count_server_handshake_called < 2)
    {
        f.server_io_service.run_one();
    }
}
END_TEST

START_TEST(test_server_handshake_eof)
{
    TlsServiceServerTestFixture f;
    f.server_engine->next_result = gu::AsioStreamEngine::eof;
    f.server_io_service.run_one();
    // Acceptor silently discards accepted socket which fails during
    // handshake and restarts async accept.
    ck_assert(f.acceptor_handler->accepted_socket() == 0);
    ck_assert(f.server_engine->count_server_handshake_called == 1);
}
END_TEST

START_TEST(test_server_handshake_eof2)
{
    TlsServiceServerTestFixture f;
    // First op causes accept handler to restart server handshake call.
    // The EOF will now handled in server handshake handler.
    f.server_engine->next_result = gu::AsioStreamEngine::want_write;
    f.server_io_service.run_one();
    f.server_engine->next_result = gu::AsioStreamEngine::eof;
    f.server_io_service.run_one();
    // Acceptor silently discards accepted socket which fails during
    // handshake and restarts async accept.
    ck_assert(f.acceptor_handler->accepted_socket() == 0);
    ck_assert(f.server_engine->count_server_handshake_called == 2);
}
END_TEST

START_TEST(test_server_handshake_error)
{
    TlsServiceServerTestFixture f;
    f.server_engine->next_result = gu::AsioStreamEngine::error;
    f.server_engine->next_error = EPIPE;
    f.server_io_service.run_one();
    // Acceptor silently discards accepted socket which fails during
    // handshake and restarts async accept.
    ck_assert(f.acceptor_handler->accepted_socket() == 0);
    ck_assert(f.server_engine->count_server_handshake_called == 1);
}
END_TEST

START_TEST(test_server_handshake_error2)
{
    TlsServiceServerTestFixture f;
    // First op causes accept handler to restart server handshake call.
    // The error will now handled in server handshake handler.
    f.server_engine->next_result = gu::AsioStreamEngine::want_write;
    f.server_io_service.run_one();
    f.server_engine->next_result = gu::AsioStreamEngine::error;
    f.server_engine->next_error = EPIPE;
    f.server_io_service.run_one();
    // Acceptor silently discards accepted socket which fails during
    // handshake and restarts async accept.
    ck_assert(f.acceptor_handler->accepted_socket() == 0);
    ck_assert(f.server_engine->count_server_handshake_called == 2);
}
END_TEST

START_TEST(test_read_want_read)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.socket->async_write(cbs, f.socket_handler);
    f.client_io_service.run_one();
    f.server_engine->next_result = gu::AsioStreamEngine::want_read;
    std::array<char, 4> buf;
    f.acceptor_handler->accepted_socket()->async_read(
        gu::AsioMutableBuffer(buf.data(), buf.size()),
        f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_read_called == 1);
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_read() == 4);
    // Write socket to make accepted socket readable, but do not start
    // async read to simulate stream engine internal operation.
    f.socket->async_write(cbs, f.socket_handler);
    f.client_io_service.reset();
    f.client_io_service.run_one();
    f.server_engine->next_result = gu::AsioStreamEngine::success;
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_read_called == 2);
    // Extra read should just call read() but the communication should
    // be internal, the handler should not see received data.
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_read() == 4);
}
END_TEST

START_TEST(test_read_want_write)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.socket->async_write(cbs, f.socket_handler);
    f.client_io_service.run_one();
    f.server_engine->next_result = gu::AsioStreamEngine::want_write;
    std::array<char, 4> buf;
    f.acceptor_handler->accepted_socket()->async_read(
        gu::AsioMutableBuffer(buf.data(), buf.size()),
        f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_read_called == 1);
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_read() == 4);
    f.server_io_service.run_one();
    // The result want_write means that the previous operation
    // (in this case read) must be called once again once the
    // socket becomes writable.
    ck_assert(f.server_engine->count_read_called == 2);
}
END_TEST

START_TEST(test_read_eof)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);
    f.socket->close();
    std::array<char, 1> buf;
    f.acceptor_handler->accepted_socket()->async_read(
        gu::AsioMutableBuffer(buf.data(), buf.size()),
        f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_read_called == 1);
    ck_assert(f.acceptor_handler->accepted_handler()->last_error_code().is_eof());
}
END_TEST

START_TEST(test_read_error)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);
    // Socket close makes the socket readable, but we override
    // the return value with error.
    f.socket->close();
    f.server_engine->next_result = gu::AsioStreamEngine::error;
    f.server_engine->next_error = EPIPE;
    std::array<char, 1> buf;
    f.acceptor_handler->accepted_socket()->async_read(
        gu::AsioMutableBuffer(buf.data(), buf.size()),
        f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_read_called == 1);
    ck_assert(f.acceptor_handler->accepted_handler()->last_error_code().value() == EPIPE);
}
END_TEST

START_TEST(test_write_want_read)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    f.server_engine->next_result = gu::AsioStreamEngine::want_read;
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.acceptor_handler->accepted_socket()->async_write(
        cbs, f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.server_engine->count_write_called == 1);
    // Write to client socket to make server side socket readable
    f.socket->async_write(cbs, f.socket_handler);
    f.client_io_service.reset();
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->bytes_written() == 4);
    // Now the server side socket should become readable and
    // the second call to write should happen.
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.server_engine->count_write_called == 2);
}
END_TEST

START_TEST(test_write_want_write)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    f.server_engine->next_result = gu::AsioStreamEngine::want_write;
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.acceptor_handler->accepted_socket()->async_write(
        cbs, f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.server_engine->count_write_called == 1);
    // Now the server side socket should remain writable and the
    // the second call to write should happen.
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.server_engine->count_write_called == 2);
}
END_TEST

START_TEST(test_write_eof)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    f.server_engine->next_result = gu::AsioStreamEngine::want_read;
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.acceptor_handler->accepted_socket()->async_write(
        cbs, f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.server_engine->count_write_called == 1);
    // Write to client socket to make server side socket readable
    f.socket->async_write(cbs, f.socket_handler);
    f.client_io_service.reset();
    f.client_io_service.run_one();
    ck_assert(f.socket_handler->bytes_written() == 4);
    f.server_engine->next_result = gu::AsioStreamEngine::eof;
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_write_called == 2);
    ck_assert(f.acceptor_handler->accepted_handler()->last_error_code().is_eof());
}
END_TEST

START_TEST(test_write_error)
{
    TlsServiceServerTestFixture f;
    f.server_io_service.run_one();
    ck_assert(f.acceptor_handler->accepted_socket() != 0);

    f.server_engine->next_result = gu::AsioStreamEngine::error;
    f.server_engine->next_error = EPIPE;
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer("writ", 4);
    cbs[1] = gu::AsioConstBuffer();
    f.acceptor_handler->accepted_socket()->async_write(
        cbs, f.acceptor_handler->accepted_handler());
    f.server_io_service.run_one();
    ck_assert(f.server_engine->count_write_called == 1);
    // Write will succeed before the error is injected, so there will be
    // some bytes written.
    ck_assert(f.acceptor_handler->accepted_handler()->bytes_written() == 4);
    ck_assert(f.acceptor_handler->accepted_handler()->last_error_code().value() == EPIPE);
}
END_TEST

//
// Datagram
//

class MockDatagramSocketHandler : public gu::AsioDatagramSocketHandler
{
public:
    MockDatagramSocketHandler()
        : gu::AsioDatagramSocketHandler()
        , bytes_read_()
    { }

    virtual void read_handler(gu::AsioDatagramSocket&, const gu::AsioErrorCode&,
                              size_t bytes_transferred) GALERA_OVERRIDE
    {
        bytes_read_ += bytes_transferred;
    }

    size_t bytes_read() const { return bytes_read_; }
private:
    size_t bytes_read_;
};

START_TEST(test_datagram_socket)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://127.0.0.1:0");
    auto socket(io_service.make_datagram_socket(uri));
}
END_TEST

START_TEST(test_datagram_open)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://127.0.0.1:0");
    auto socket(io_service.make_datagram_socket(uri));
    socket->open(uri);
}
END_TEST

START_TEST(test_datagram_connect)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://127.0.0.1:0");
    auto socket(io_service.make_datagram_socket(uri));
    socket->connect(uri);
}
END_TEST

START_TEST(test_datagram_open_connect)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://127.0.0.1:0");
    auto socket(io_service.make_datagram_socket(uri));
    socket->open(uri);
    socket->connect(uri);
}
END_TEST

START_TEST(test_datagram_connect_multicast)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://239.255.0.1:0");
    auto socket(io_service.make_datagram_socket(uri));
    socket->connect(uri);
    gu::URI bound_uri(socket->local_addr());
    auto bound_addr(gu::make_address(bound_uri.get_host()));
    ck_assert(bound_addr.is_v4());
    ck_assert_msg(bound_addr.to_v4().is_multicast(), "not datagram: %s",
                  bound_uri.to_string().c_str());
}
END_TEST

START_TEST(test_datagram_connect_multicast_local_if)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://239.255.0.1:0?socket.if_addr=127.0.0.1");
    auto socket(io_service.make_datagram_socket(uri));
    socket->connect(uri);
    gu::URI bound_uri(socket->local_addr());
    auto bound_addr(gu::make_address(bound_uri.get_host()));
    ck_assert(bound_addr.is_v4());
    ck_assert_msg(bound_addr.to_v4().is_multicast(), "not datagram: %s",
                  bound_uri.to_string().c_str());
}
END_TEST

void test_datagram_send_to_and_async_read_common(
    gu::AsioIoService& io_service,
    gu::AsioDatagramSocket& socket,
    const std::shared_ptr<MockDatagramSocketHandler>& handler)
{
    gu::URI local_uri(socket.local_addr());
    const char* hdr = "hdr";
    const char* data = "data";
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    gu::URI udp_uri("udp://127.0.0.1:0?socket.if_addr=127.0.0.1");
    auto sender_socket(io_service.make_datagram_socket(udp_uri));
    sender_socket->connect(udp_uri);
    sender_socket->send_to(cbs,
                           gu::make_address(local_uri.get_host()),
                           gu::from_string<unsigned short>(
                               local_uri.get_port()));

    char read_buf[7];
    socket.async_read(gu::AsioMutableBuffer(read_buf, sizeof(read_buf)), handler);
    while (handler->bytes_read() != sizeof(read_buf))
    {
        io_service.run_one();
    }
}

START_TEST(test_datagram_send_to_and_async_read)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://127.0.0.1:0");
    auto handler(std::make_shared<MockDatagramSocketHandler>());
    auto socket(io_service.make_datagram_socket(uri));
    socket->open(uri);
    socket->connect(uri);

    test_datagram_send_to_and_async_read_common(io_service, *socket, handler);
}
END_TEST

START_TEST(test_datagram_send_to_and_async_read_multicast)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://239.255.0.1:0?socket.if_addr=127.0.0.1");
    auto handler(std::make_shared<MockDatagramSocketHandler>());
    auto socket(io_service.make_datagram_socket(uri));
    socket->open(uri);
    socket->connect(uri);

    test_datagram_send_to_and_async_read_common(io_service, *socket, handler);
}
END_TEST

START_TEST(test_datagram_write_multicast)
{
    gu::AsioIoService io_service;
    gu::URI uri("udp://239.255.0.1:0?socket.if_addr=127.0.0.1");
    auto socket(io_service.make_datagram_socket(uri));
    socket->open(uri);
    socket->connect(uri);
    const char* hdr = "hdr";
    const char* data = "data";
    std::array<gu::AsioConstBuffer, 2> cbs;
    cbs[0] = gu::AsioConstBuffer(hdr, strlen(hdr));
    cbs[1] = gu::AsioConstBuffer(data, strlen(data));
    socket->write(cbs);
}
END_TEST

//
// Steady timer
//

class MockSteadyTimerHandler : public gu::AsioSteadyTimerHandler
{
public:
    MockSteadyTimerHandler()
        : gu::AsioSteadyTimerHandler()
        , called_()
    { }
    void handle_wait(const gu::AsioErrorCode&)
    {
        called_ = true;
    }
    bool called() const { return called_; }
private:
    bool called_;
};

START_TEST(test_steady_timer)
{
    gu::AsioIoService io_service;
    auto handler(std::make_shared<MockSteadyTimerHandler>());
    gu::AsioSteadyTimer timer(io_service);

    timer.expires_from_now(std::chrono::milliseconds(50));
    timer.async_wait(handler);
#ifdef TEST_STREADY_TIMER_CHECK_DURATION
    auto start(std::chrono::steady_clock::now());
#endif
    io_service.run_one();
#ifdef TEST_STREADY_TIMER_CHECK_DURATION
    auto stop(std::chrono::steady_clock::now());
#endif
    ck_assert(handler->called());
#ifdef TEST_STREADY_TIMER_CHECK_DURATION
    // Don't check duration by default. The operation sometimes take less than
    // 50msec for some reason.
    ck_assert(
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start)
        >= std::chrono::milliseconds(50),
        "Timer duration less than 50 milliseconds %zu",
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count());
#endif
}
END_TEST

Suite* gu_asio_suite()
{
    Suite* s(suite_create("gu::asio"));
    TCase* tc;

    tc = tcase_create("test_make_address_v4");
    tcase_add_test(tc, test_make_address_v4);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_make_address_v6_link_local");
    tcase_add_test(tc, test_make_address_v6_link_local);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_make_address_v6_link_local_with_scope_id");
    tcase_add_test(tc, test_make_address_v6_link_local_with_scope_id);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_error_code_success");
    tcase_add_test(tc, test_error_code_success);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_error_code_error");
    tcase_add_test(tc, test_error_code_error);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_io_service");
    tcase_add_test(tc, test_io_service);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_const_buffer");
    tcase_add_test(tc, test_const_buffer);
    suite_add_tcase(s, tc);

    //
    // TCP
    //

    tc = tcase_create("test_tcp_socket");
    tcase_add_test(tc, test_tcp_socket);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_socket_receive_buffer_size_unopened");
    tcase_add_test(tc, test_tcp_socket_receive_buffer_size_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_socket_receive_buffer_size");
    tcase_add_test(tc, test_tcp_socket_receive_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_socket_send_buffer_size_unopened");
    tcase_add_test(tc, test_tcp_socket_send_buffer_size_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_socket_send_buffer_size");
    tcase_add_test(tc, test_tcp_socket_send_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_read_unopened");
    tcase_add_test(tc, test_tcp_read_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_write_unopened");
    tcase_add_test(tc, test_tcp_write_unopened);
    suite_add_tcase(s, tc);


    tc = tcase_create("test_tcp_acceptor");
    tcase_add_test(tc, test_tcp_acceptor);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_acceptor_listen");
    tcase_add_test(tc, test_tcp_acceptor_listen);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_acceptor_receive_buffer_size_unopened");
    tcase_add_test(tc, test_tcp_acceptor_receive_buffer_size_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_acceptor_receive_buffer_size");
    tcase_add_test(tc, test_tcp_acceptor_receive_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_acceptor_send_buffer_size_unopened");
    tcase_add_test(tc, test_tcp_acceptor_send_buffer_size_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_acceptor_send_buffer_size");
    tcase_add_test(tc, test_tcp_acceptor_send_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_connect");
    tcase_add_test(tc, test_tcp_connect);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_connect_twice");
    tcase_add_test(tc, test_tcp_connect_twice);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_async_read_write");
    tcase_add_test(tc, test_tcp_async_read_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_async_read_write_large");
    tcase_add_test(tc, test_tcp_async_read_write_large);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_async_read_write_small_large");
    tcase_add_test(tc, test_tcp_async_read_write_small_large);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_async_read_from_client_write_from_server");
    tcase_add_test(tc, test_tcp_async_read_from_client_write_from_server);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_write_twice_wo_handling");
    tcase_add_test(tc, test_tcp_write_twice_wo_handling);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_close_client");
    tcase_add_test(tc, test_tcp_close_client);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_close_server");
    tcase_add_test(tc, test_tcp_close_server);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_tcp_get_tcp_info");
    tcase_add_test(tc, test_tcp_get_tcp_info);
    suite_add_tcase(s, tc);

#ifdef GALERA_HAVE_SSL
    //
    // SSL
    //

    tc = tcase_create("test_ssl_io_service");
    tcase_add_test(tc, test_ssl_io_service);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_socket");
    tcase_add_test(tc, test_ssl_socket);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_socket_receive_buffer_unopened");
    tcase_add_test(tc, test_ssl_socket_receive_buffer_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_socket_receive_buffer_size");
    tcase_add_test(tc, test_ssl_socket_receive_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_socket_send_buffer_unopened");
    tcase_add_test(tc, test_ssl_socket_send_buffer_unopened);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_socket_send_buffer_size");
    tcase_add_test(tc, test_ssl_socket_send_buffer_size);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_acceptor");
    tcase_add_test(tc, test_ssl_acceptor);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_connect");
    tcase_add_test(tc, test_ssl_connect);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_connect_twice");
    tcase_add_test(tc, test_ssl_connect_twice);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_async_read_write");
    tcase_add_test(tc, test_ssl_async_read_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_async_read_write_large");
    tcase_add_test(tc, test_ssl_async_read_write_large);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_async_read_write_small_large");
    tcase_add_test(tc, test_ssl_async_read_write_small_large);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_async_read_from_client_write_from_server");
    tcase_add_test(tc, test_ssl_async_read_from_client_write_from_server);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_write_twice_wo_handling");
    tcase_add_test(tc, test_ssl_write_twice_wo_handling);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_close_client");
    tcase_add_test(tc, test_ssl_close_client);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_close_server");
    tcase_add_test(tc, test_ssl_close_server);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_get_tcp_info");
    tcase_add_test(tc, test_ssl_get_tcp_info);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_ssl_compression_option");
    tcase_add_test(tc, test_ssl_compression_option);
    suite_add_tcase(s, tc);

#endif // GALERA_HAVE_SSL

    tc = tcase_create("test_client_handshake_want_read");
    tcase_add_test(tc, test_client_handshake_want_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_client_handshake_want_write");
    tcase_add_test(tc, test_client_handshake_want_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_client_handshake_eof");
    tcase_add_test(tc, test_client_handshake_eof);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_client_handshake_eof2");
    tcase_add_test(tc, test_client_handshake_eof2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_client_handshake_error");
    tcase_add_test(tc, test_client_handshake_error);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_client_handshake_error2");
    tcase_add_test(tc, test_client_handshake_error2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_want_read");
    tcase_add_test(tc, test_server_handshake_want_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_want_write");
    tcase_add_test(tc, test_server_handshake_want_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_eof");
    tcase_add_test(tc, test_server_handshake_eof);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_eof2");
    tcase_add_test(tc, test_server_handshake_eof2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_error");
    tcase_add_test(tc, test_server_handshake_error);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_server_handshake_error2");
    tcase_add_test(tc, test_server_handshake_error2);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_read_want_read");
    tcase_add_test(tc, test_read_want_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_read_want_write");
    tcase_add_test(tc, test_read_want_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_read_eof");
    tcase_add_test(tc, test_read_eof);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_read_error");
    tcase_add_test(tc, test_read_error);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_want_read");
    tcase_add_test(tc, test_write_want_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_want_write");
    tcase_add_test(tc, test_write_want_write);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_eof");
    tcase_add_test(tc, test_write_eof);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_write_error");
    tcase_add_test(tc, test_write_error);
    suite_add_tcase(s, tc);

    //
    // Datagram
    //

    tc = tcase_create("test_datagram_socket");
    tcase_add_test(tc, test_datagram_socket);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_open");
    tcase_add_test(tc, test_datagram_open);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_connect");
    tcase_add_test(tc, test_datagram_connect);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_open_connect");
    tcase_add_test(tc, test_datagram_open_connect);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_connect_multicast");
    tcase_add_test(tc, test_datagram_connect_multicast);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_connect_multicast_local_if");
    tcase_add_test(tc, test_datagram_connect_multicast_local_if);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_send_to_and_async_read");
    tcase_add_test(tc, test_datagram_send_to_and_async_read);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_send_to_and_async_read_multicast");
    tcase_add_test(tc, test_datagram_send_to_and_async_read_multicast);
    suite_add_tcase(s, tc);

    tc = tcase_create("test_datagram_write_multicast");
    tcase_add_test(tc, test_datagram_write_multicast);
    suite_add_tcase(s, tc);

    //
    // Steady timer
    //

    tc = tcase_create("test_steady_timer");
    tcase_add_test(tc, test_steady_timer);
    suite_add_tcase(s, tc);

    return s;
}

