//
// Copyright (C) 2020 Codership Oy <info@codership.com>
//

/** @file gu_asio_stream_react.hpp
 *
 * Asio stream socket implementations based on reactive model.
 * The AsioStreamReact controls TCP socket and reacts on
 * event notifications from IO service. The event handling is
 * then delegated to AsioStreamEngine object, whose
 * responsibility is to handle the events from socket and
 * forward them to AsioSocketHandler when appropriate.
 */

#ifndef GU_ASIO_STREAM_REACT_HPP
#define GU_ASIO_STREAM_REACT_HPP

#ifndef GU_ASIO_IMPL
#error This header should not be included directly.
#endif // GU_ASIO_IMPL

#include "gu_asio.hpp"
#include "gu_asio_stream_engine.hpp"

#include "gu_buffer.hpp"

#include "asio/ip/tcp.hpp"

#include <cerrno>

#include "gu_disable_non_virtual_dtor.hpp"
#include "gu_compiler.hpp"

namespace gu
{

    class AsioStreamReact : public AsioSocket
                          , public std::enable_shared_from_this<AsioStreamReact>
    {
    public:
        AsioStreamReact(AsioIoService&, const std::string&,
                        const std::shared_ptr<AsioStreamEngine>&);
        AsioStreamReact(const AsioStreamReact&) = delete;
        AsioStreamReact& operator=(const AsioStreamReact&) = delete;
        ~AsioStreamReact();

        virtual void open(const gu::URI&) GALERA_OVERRIDE;
        virtual bool is_open() const GALERA_OVERRIDE;
        virtual void close() GALERA_OVERRIDE;
        virtual void bind(const gu::AsioIpAddress&) GALERA_OVERRIDE;
        virtual void async_connect(
            const gu::URI&,
            const std::shared_ptr<AsioSocketHandler>&) GALERA_OVERRIDE;
        virtual void async_write(const std::array<AsioConstBuffer, 2>&,
                                 const std::shared_ptr<AsioSocketHandler>&)
            GALERA_OVERRIDE;
        virtual void async_read(const AsioMutableBuffer&,
                                const std::shared_ptr<AsioSocketHandler>&)
            GALERA_OVERRIDE;
        virtual void connect(const gu::URI&) GALERA_OVERRIDE;
        virtual size_t write(const AsioConstBuffer&) GALERA_OVERRIDE;
        virtual size_t read(const AsioMutableBuffer&) GALERA_OVERRIDE;
        virtual std::string local_addr() const GALERA_OVERRIDE;
        virtual std::string remote_addr() const GALERA_OVERRIDE;
        virtual void set_receive_buffer_size(size_t) GALERA_OVERRIDE;
        virtual size_t get_receive_buffer_size() GALERA_OVERRIDE;
        virtual void set_send_buffer_size(size_t) GALERA_OVERRIDE;
        virtual size_t get_send_buffer_size() GALERA_OVERRIDE;
        virtual struct tcp_info get_tcp_info() GALERA_OVERRIDE;


        // Handlers for ASIO service.
        void connect_handler(const std::shared_ptr<AsioSocketHandler>&,
                             const asio::error_code& ec);
        void client_handshake_handler(const std::shared_ptr<AsioSocketHandler>&,
                                      const asio::error_code&);
        void server_handshake_handler(
            const std::shared_ptr<AsioAcceptor>& acceptor,
            const std::shared_ptr<AsioAcceptorHandler>& acceptor_handler,
            const asio::error_code& ec);
        void read_handler(const std::shared_ptr<AsioSocketHandler>&,
                          const asio::error_code&);
        void write_handler(const std::shared_ptr<AsioSocketHandler>&,
                           const asio::error_code&);
    private:
        friend class AsioAcceptorReact;

        void assign_addresses();
        void prepare_engine();
        // Start async read if not in progress. May be called several times
        // without handling read in between.
        template <typename Fn, typename ...FnArgs>
        void start_async_read(Fn fn, FnArgs... args);
        // Start async write if not in progress. May be called several times
        // without handling a write in between.
        template <typename Fn, typename ...FnArgs>
        void start_async_write(Fn, FnArgs...);

        void complete_read_op(const std::shared_ptr<AsioSocketHandler>&,
                              size_t bytes_transferred);
        void complete_write_op(const std::shared_ptr<AsioSocketHandler>&,
                               size_t bytes_transferred);
        void handle_read_handler_error(
            const std::shared_ptr<AsioSocketHandler>&,
            const AsioErrorCode&);
        void handle_write_handler_error(
            const std::shared_ptr<AsioSocketHandler>&,
            const AsioErrorCode&);

        void set_non_blocking(bool);

        void shutdown();
        std::string debug_print() const;

        // Data members
        AsioIoService& io_service_;
        asio::ip::tcp::socket socket_;
        std::string scheme_;
        std::shared_ptr<AsioStreamEngine> engine_;
        std::string local_addr_;
        std::string remote_addr_;
        bool connected_;
        bool non_blocking_;

        // Flags and state for operations in progress.
        static const int read_in_progress = 0x1;
        static const int write_in_progress = 0x2;
        static const int shutdown_in_progress = 0x4;
        // static const int client_handshake_in_progress = 0x4;
        // static const int server_handshake_in_progress = 0x8;
        static const int engine_wants_read = 0x10;
        static const int engine_wants_write = 0x20;
        int in_progress_;

        class ReadContext
        {
        public:
            ReadContext()
                : buf_()
                , bytes_transferred_()
                , read_completion_()
            { }
            ReadContext(const AsioMutableBuffer& buf)
                : buf_(buf)
                , bytes_transferred_()
                , read_completion_()
            { }
            ReadContext(const ReadContext&) = default;
            ReadContext& operator=(const ReadContext&) = default;

            const AsioMutableBuffer& buf() const { return buf_ ;}
            size_t bytes_transferred() const { return bytes_transferred_; }
            void read_completion(size_t read_completion)
            {
                assert(read_completion <= left_to_read());
                read_completion_ = read_completion;
            }
            size_t read_completion() const { return read_completion_; }
            void inc_bytes_transferred(size_t val) { bytes_transferred_ += val; }
            // Bytes left to read on async read operation. This is either
            // remaining space left in the input buffer, or number of
            // bytes requested to read as indicated by read completion.
            size_t left_to_read() const
            {
                return (read_completion_ ? read_completion_ :
                        buf_.size() - bytes_transferred_);
            }
            void reset()
            {
                buf_ = AsioMutableBuffer();
                bytes_transferred_ = 0;
                read_completion_ = 0;
            }
        private:
            AsioMutableBuffer buf_;
            size_t bytes_transferred_;
            size_t read_completion_;
        } read_context_;

        class WriteContext
        {
        public:
            WriteContext() : buf_(), bytes_transferred_() { }
            WriteContext(const std::array<AsioConstBuffer, 2>& bufs)
                : buf_()
                , bytes_transferred_()
            {
                for (auto i(bufs.begin()); i != bufs.end(); ++i)
                {
                    buf_.insert(buf_.end(),
                                reinterpret_cast<const char*>(i->data()),
                                reinterpret_cast<const char*>(i->data())
                                + i->size());
                }
            }
            WriteContext(const WriteContext&) = default;
            WriteContext& operator=(const WriteContext&) = default;
            const gu::Buffer& buf() const { return buf_; }
            size_t bytes_transferred() const { return bytes_transferred_; }
            void inc_bytes_transferred(size_t val) { bytes_transferred_ += val; }
            void reset()
            {
                buf_.clear();
                bytes_transferred_ = 0;
            }
        private:
            // A temporary buffer to copy data in before writing it to
            // socket. Trying to do scatter/gather IO with stream processing
            // engine which might involve TLS/SSL does not make any sense
            // as scatter/gather is not implemented, at least for OpenSSL.
            // Therefore we write the user provided buffer into single
            // continuous memory block which is passed to write function.
            // Galera typically sends relatively small messages in
            // async mode so the memory overhead should be minimal.
            // CPU overhead pales in comparison to encryption overhead,
            // so we can ignore it here.
            //
            // Another option would be writing buffers one by one, but given
            // the nature of gcomm messages (short header, payload), this
            // would increase the number of system calls significantly, which
            // quite likely would lead to higher overhead than buffer copy.
            gu::Buffer buf_;
            size_t bytes_transferred_;
        } write_context_;
    };

    class AsioAcceptorReact
        : public AsioAcceptor
        , public std::enable_shared_from_this<AsioAcceptorReact>
    {
    public:
        AsioAcceptorReact(AsioIoService&, const std::string& scheme);
        virtual void open(const gu::URI&) GALERA_OVERRIDE;
        virtual void listen(const gu::URI&) GALERA_OVERRIDE;
        virtual void close() GALERA_OVERRIDE;
        virtual void async_accept(
            const std::shared_ptr<AsioAcceptorHandler>&,
            const std::shared_ptr<AsioStreamEngine>& engine = nullptr)
            GALERA_OVERRIDE;
        virtual std::shared_ptr<AsioSocket> accept() GALERA_OVERRIDE;
        virtual std::string listen_addr() const GALERA_OVERRIDE;
        virtual unsigned short listen_port() const GALERA_OVERRIDE;
        virtual void set_receive_buffer_size(size_t) GALERA_OVERRIDE;
        virtual size_t get_receive_buffer_size() GALERA_OVERRIDE;
        virtual void set_send_buffer_size(size_t) GALERA_OVERRIDE;
        virtual size_t get_send_buffer_size() GALERA_OVERRIDE;

        // ASIO handlers
        void accept_handler(const std::shared_ptr<AsioStreamReact>&,
                            const std::shared_ptr<AsioAcceptorHandler>&,
                            const asio::error_code&);
    private:
        std::string debug_print() const;
        AsioIoService& io_service_;
        asio::ip::tcp::acceptor acceptor_;
        std::string scheme_;
        bool listening_;
        std::shared_ptr<AsioStreamEngine> engine_;
    };
}

#include "gu_enable_non_virtual_dtor.hpp"

#endif // GU_ASIO_STREAM_REACT_HPP
