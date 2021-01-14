//
// Copyright (C) 2014-2020 Codership Oy <info@codership.com>
//


//
// Common ASIO methods and configuration options for Galera
//

#ifndef GU_ASIO_HPP
#define GU_ASIO_HPP

#include "gu_config.hpp"
#include "gu_uri.hpp"

#include <netinet/tcp.h> // tcp_info

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace gu
{
    // URI schemes for networking
    namespace scheme
    {
        const std::string tcp("tcp"); /// TCP scheme
        const std::string udp("udp"); /// UDP scheme
        const std::string ssl("ssl"); /// SSL scheme
        const std::string def("tcp"); /// default scheme (TCP)
    }

#ifdef GALERA_HAVE_SSL
    //
    // SSL
    //

    // Configuration options for sockets
    namespace conf
    {
        /// Enable SSL explicitly
        const std::string use_ssl("socket.ssl");
        /// SSL cipher list
        const std::string ssl_cipher("socket.ssl_cipher");
        /// SSL compression algorithm
        const std::string ssl_compression("socket.ssl_compression");
        /// SSL private key file
        const std::string ssl_key("socket.ssl_key");
        /// SSL certificate file
        const std::string ssl_cert("socket.ssl_cert");
        /// SSL CA file
        const std::string ssl_ca("socket.ssl_ca");
        /// SSL password file
        const std::string ssl_password_file("socket.ssl_password_file");
    }


    // register ssl parameters to config
    void ssl_register_params(gu::Config&);

    // initialize defaults, verify set options
    void ssl_init_options(gu::Config&);
#else
    static inline void ssl_register_params(gu::Config&) { }
    static inline void ssl_init_options(gu::Config&) { }
#endif // GALERA_HAVE_SSL

    //
    // Address manipulation helpers
    //

    /**
     * @class AsioIpAddressV4
     *
     * A wrapper around asio::ip::address_v4
     */
    class AsioIpAddressV4
    {
    public:
        AsioIpAddressV4();
        AsioIpAddressV4(const AsioIpAddressV4&);
        AsioIpAddressV4& operator=(AsioIpAddressV4);
        ~AsioIpAddressV4();
        bool is_multicast() const;
        class Impl;
        Impl& impl();
        const Impl& impl() const;
    private:
        std::unique_ptr<Impl> impl_;
    };

    /**
     * @class AsioIpAddressV6
     *
     * A wrapper around asio::ip::address_v6
     */
    class AsioIpAddressV6
    {
    public:
        AsioIpAddressV6();
        AsioIpAddressV6(const AsioIpAddressV6&);
        AsioIpAddressV6& operator=(AsioIpAddressV6);
        ~AsioIpAddressV6();
        bool is_link_local() const;
        unsigned long scope_id() const;
        bool is_multicast() const;
        class Impl;
        Impl& impl();
        const Impl& impl() const;
    private:
        std::unique_ptr<Impl> impl_;
    };

    /**
     * @class AsioIpAddressV6
     *
     * A wrapper around asio::ip::address
     */
    class AsioIpAddress
    {
    public:
        class Impl;
        AsioIpAddress();
        AsioIpAddress(const AsioIpAddress&);
        AsioIpAddress& operator=(AsioIpAddress);
        ~AsioIpAddress();
        bool is_v4() const;
        bool is_v6() const;
        AsioIpAddressV4 to_v4() const;
        AsioIpAddressV6 to_v6() const;
        Impl& impl();
        const Impl& impl() const;
    private:
        std::unique_ptr<Impl> impl_;
    };

    // Return any address string.
    std::string any_addr(const AsioIpAddress& addr);

    // Escape address string. Surrounds IPv6 address with [].
    // IPv4 addresses not affected.
    std::string escape_addr(const AsioIpAddress& addr);

    // Unescape address string. Remove [] from around the address if found.
    std::string unescape_addr(const std::string& addr);

    // Construct asio::ip::address from address string
    AsioIpAddress make_address(const std::string& addr);


    class AsioMutableBuffer
    {
    public:
        AsioMutableBuffer() : data_(), size_() { }
        AsioMutableBuffer(void* data, size_t size)
            : data_(data)
            , size_(size)
        { }
        void* data() const { return  data_; }
        size_t size() const { return size_; }
    private:
        void* data_;
        size_t size_;
    };

    class AsioConstBuffer
    {
    public:
        AsioConstBuffer()
            : data_()
            , size_()
        { }
        AsioConstBuffer(const void* data, size_t size)
            : data_(data)
            , size_(size)
        { }
        const void* data() const { return data_; }
        size_t size() const { return size_; }
    private:
        const void* data_;
        size_t size_;
    };


    class AsioErrorCategory;
    class AsioErrorCode
    {
    public:
        /**
         * A default constructor. Constructs success error code.
         */
        AsioErrorCode();
        /**
         * A constructor to generate error codes from system error codes.
         */
        AsioErrorCode(int value);

        /**
         * A constructor to generate error codes from asio errors.
         */
        AsioErrorCode(int value, const AsioErrorCategory& category)
            : value_(value)
            , category_(&category)
            , wsrep_category_()
        { }

        /**
         * Return error number.
         */
        int value() const { return value_; }

        const AsioErrorCategory* category() const { return category_; }

        /**
         * Return human readable error message.
         */
        std::string message() const;

        operator bool() const { return value_; }

        bool operator!() const { return value_ == 0; }

        /**
         * Return true if the error is end of file.
         */
        bool is_eof() const;

        /**
         * Return true if the error belongs to wsrep category.
         */
        bool is_wsrep() const;

        /**
         * Return true if the error is system error.
         */
        bool is_system() const;

    private:
        int value_;
        const AsioErrorCategory* category_;
        const void* wsrep_category_;
    };

    std::ostream& operator<<(std::ostream&, const AsioErrorCode&);

    /*
     * Helper to determine if the error code originates from an
     * event which happens often and pollutes logs but for which the error
     * does not provide any helpful information.
     *
     * Errors which happen frequently during cluster configuration changes
     * and when connections break are considered verbose.
     *
     * Certain SSL errors such as 'short read' error are considered verbose.
     *
     * All errors which originate from TLS service hooks are considered
     * verbose, it is up to application report them if appropriate.
     */
    bool is_verbose_error(const AsioErrorCode&);

    // TODO: Hide extra error info from public interface. It should be
    // called internally by calls which produce human readable error messages.
#ifdef GALERA_HAVE_SSL
    std::string extra_error_info(const gu::AsioErrorCode& ec);
#else // GALERA_HAVE_SSL
    static inline std::string extra_error_info(const gu::AsioErrorCode&)
    { return ""; }
#endif // GALERA_HAVE_SSL
    class AsioSocket;
    /**
     * Abstract interface for asio socket handlers.
     */
    class AsioSocketHandler
    {
    public:
        virtual ~AsioSocketHandler() { }
        /**
         * This will be called after asynchronous connection to the
         * remote endpoint after call to AsioSocket::async_connect()
         * completes.
         *
         * All internal protocol handshakes (e.g. SSL) will be completed
         * before this handler is called.
         *
         * @param socket Reference to socket which initiated the call.
         * @param ec     Error code.
         */
        virtual void connect_handler(AsioSocket& socket,
                                     const AsioErrorCode& ec) = 0;
        virtual void write_handler(AsioSocket&,
                                   const AsioErrorCode&,
                                   size_t bytes_transferred) = 0;
        /**
         * This call is called every time more data has been written
         * into receive buffer submitted via async_read() call.
         * The return value of the call should be maximum number of
         * bytes to be transferred before the read is considered
         * complete and read_handler() will be called.
         *
         * If the returned value is larger than the available space in
         * read buffer, the maximum number of bytes to be transferred
         * will be the available space in read buffer. It is up to application
         * to resize the read buffer in read_handler() and restart async read
         * if the available space was not enough to contain the whole message.
         *
         * @param socket Stream socket associated to this handler.
         * @param ec Error code.
         * @param bytes_transferred Number of bytes transferred so far.
         *
         * @return Maximum number of bytes to read to complete the
         *         read operation.
         */
        virtual size_t read_completion_condition(AsioSocket& socket,
                                                 const AsioErrorCode&,
                                                 size_t bytes_transferred) = 0;
        virtual void read_handler(AsioSocket&, const AsioErrorCode&,
                                  size_t bytes_transferred) = 0;

    };

    /**
     * @class AsioSocket
     *
     * Abstract interface for stream socket implementations.
     *
     * Although the interface provides both sync and async operations,
     * those should never be mixed. If the socket is connected
     * via connect() call (or accepted via AsioAcceptor::accept() call),
     * the underlying implementation uses blocking calls for
     * reading and writing.  On the other hand, if async_connect()
     * or AsioAcceptor::async_accept() is used, the underlying implementation
     * uses non-blocking operations.
     */
    class AsioSocket
    {
    public:
        AsioSocket() { }

        AsioSocket(const AsioSocket&) = delete;
        AsioSocket& operator=(const AsioSocket&) = delete;

        virtual ~AsioSocket() { }

        /**
         * Open the socket without connecting.
         */
        virtual void open(const gu::URI& uri) = 0;

        /**
         * Return true if the socket is open.
         */
        virtual bool is_open() const = 0;

        /**
         * Close the socket.
         */
        virtual void close() = 0;

        /**
         * Bind the socket to interface specified by address.
         */
        virtual void bind(const gu::AsioIpAddress&) = 0;

        // Asynchronous operations

        virtual void async_connect(
            const gu::URI& uri,
            const std::shared_ptr<AsioSocketHandler>& handler) = 0;
        /**
         * Call once. Next call can be made after socket handler is called
         * with bytes transferred equal to last write size.
         */
        virtual void async_write(
            const std::array<AsioConstBuffer, 2>&,
            const std::shared_ptr<AsioSocketHandler>& handler) = 0;

        /**
         * Call once. Next call can be done from socket handler
         * read_handler or read_completion_condition.
         */
        virtual void async_read(
            const AsioMutableBuffer&,
            const std::shared_ptr<AsioSocketHandler>& handler) = 0;

        // Synchronous operations

        /**
         * Connect to remote endpoint specified by uri.
         *
         * @throw gu::Exception in case of error.
         */
        virtual void connect(const gu::URI& uri) = 0;

        /**
         * Write contents of buffer into socket. This call blocks until
         * all data has been written or error occurs.
         *
         * @throw gu::Exception in case of error.
         */
        virtual size_t write(const AsioConstBuffer& buffer) = 0;

        /**
         * Read data from socket into buffer. The value returned is the
         * number of bytes read so far.
         *
         * @throw gu::Exception in case of error.
         */
        virtual size_t read(const AsioMutableBuffer& buffer) = 0;

        // Utility operations.

        /**
         * Return address URI of local endpoint. Return empty string
         * if not connected.
         */
        virtual std::string local_addr() const = 0;

        /**
         * Return address URI of remote endpoint. Returns empty string
         * if not connected.
         */
        virtual std::string remote_addr() const = 0;

        /**
         * Set receive buffer size for the socket. This must be called
         * before the socket is connected/accepted.
         */
        virtual void set_receive_buffer_size(size_t) = 0;

        /**
         * Return currently effective receive buffer size.
         */
        virtual size_t get_receive_buffer_size() = 0;

        /**
         * Set send buffer size for the socket. This must be called
         * before the socket is connected/accepted.
         */
        virtual void set_send_buffer_size(size_t) = 0;

        /**
         * Return currently effective send buffer size.
         */
        virtual size_t get_send_buffer_size() = 0;

        /**
         * Read tcp_info struct from the underlying TCP socket.
         */
        virtual struct tcp_info get_tcp_info() = 0;
    };

    /**
     * Helper template to write buffer sequences.
     *
     * @todo This should probably be optimized by implementing
     * AsioSocket::write() overload which takes iterators to
     * buffer sequences.
     */
    template <class ConstBufferSequence>
    size_t write(AsioSocket& socket, const ConstBufferSequence& bufs)
    {
        size_t written(0);
        for (auto b(bufs.begin()); b != bufs.end(); ++b)
        {
            if (b->size() > 0)
            {
                written += socket.write(AsioConstBuffer(b->data(), b->size()));
            }
        }
        return written;
    }

    class AsioDatagramSocket;
    class AsioDatagramSocketHandler
    {
    public:
        virtual ~AsioDatagramSocketHandler() { }
        virtual void read_handler(AsioDatagramSocket&, const AsioErrorCode&,
                                  size_t bytes_transferred) = 0;
    };

    /**
     * @class AsioDatagramSocket
     *
     * Abstract interface for datagram socket implementations.
     */
    class AsioDatagramSocket
    {
    public:
        AsioDatagramSocket() { }
        virtual ~AsioDatagramSocket() { }

        /**
         * Open the socket.
         */
        virtual void open(const URI&) = 0;

        /**
         * Connect the socket to desired local endpoint. The socket
         * will be bound to endpoint specified the uri. If the uri
         * contains a multicast address, the connect will join the
         * multicast group automatically.
         */
        virtual void connect(const URI& uri) = 0;

        /**
         * Close the socket. If the socket was joined to multicast group,
         * the group is left automatically.
         */
        virtual void close() = 0;

        /**
         * Performa a write to the socket. The write is best effort only
         * and the message can be dropped because of various reasons like
         * kernel send buffer being full, network dropping the packet or
         * receiving end(s) dropping the packet for whatever reason.
         *
         * The socket must be connected before writing into it.
         * If connect() is not called, send_to() can be used to send
         * datagram into desired address.
         *
         * @param bufs Array of two buffers.
         *
         * @throw gu::Exception If an other error than message being dropped
         *        occurs, an exception containing the error code will be thrown.
         */
        virtual void write(const std::array<AsioConstBuffer, 2>& bufs) = 0;

        /**
         * Send a datagram to destination given by target. Sending a
         * message is best effort only, the message may be dropped
         * because of whatever reason and no error is given if the
         * target endpoint does not exist.
         */
        virtual void send_to(const std::array<AsioConstBuffer, 2>& bufs,
                             const AsioIpAddress& target_host,
                             unsigned short target_port) = 0;

        /**
         * Start asynchronous read from the socket. The socket handler
         * read_handler() method will be called for each complete message
         * which has been received.
         */
        virtual void async_read(const AsioMutableBuffer&,
                                const std::shared_ptr<AsioDatagramSocketHandler>& handler) = 0;

        /**
         * Return address containing the local endpoint where the socket
         * was bound to.
         *
         * @todo Maybe this should be bind addr and corresponding call
         * connected_addr() should be introduced.
         */
        virtual std::string local_addr() const = 0;
    };

    class AsioAcceptor;
    class AsioAcceptorHandler
    {
    public:
        virtual ~AsioAcceptorHandler() { }
        virtual void accept_handler(AsioAcceptor&,
                                    const std::shared_ptr<AsioSocket>&,
                                    const gu::AsioErrorCode&) = 0;
    };

    // Forward declaration for AsioAcceptor and make_socket()
    class AsioStreamEngine;


    /** @class AsioAcceptor
     *
     * Acceptor interface for stream sockets.
     */
    class AsioAcceptor
    {
    public:
        AsioAcceptor() { }
        AsioAcceptor(const AsioAcceptor&) = delete;
        AsioAcceptor& operator=(const AsioAcceptor&) = delete;
        virtual ~AsioAcceptor() { }
        virtual void open(const gu::URI& uri) = 0;
        virtual void listen(const gu::URI& uri) = 0;
        virtual void close() = 0;
        virtual void async_accept(const std::shared_ptr<AsioAcceptorHandler>&,
                                  const std::shared_ptr<AsioStreamEngine>& engine = nullptr) = 0;
        virtual std::shared_ptr<AsioSocket> accept() = 0;
        virtual std::string listen_addr() const = 0;
        virtual unsigned short listen_port() const = 0;

        /**
         * Set receive buffer size for the acceptor. This must be called
         * before listening.
         */
        virtual void set_receive_buffer_size(size_t) = 0;

        /**
         * Return currently effective receive buffer size.
         */
        virtual size_t get_receive_buffer_size() = 0;

        /**
         * Set send buffer size for the acceptor. This must be called
         * before listening.
         */
        virtual void set_send_buffer_size(size_t) = 0;

        virtual size_t get_send_buffer_size() = 0;

    };

    class AsioIoService
    {
    public:
        AsioIoService(const gu::Config& conf = gu::Config());
        ~AsioIoService();
        AsioIoService(const AsioIoService&) = delete;
        AsioIoService operator=(const AsioIoService&) = delete;

        /**
         * Load crypto context.
         */
        void load_crypto_context();

        /**
         * Run one IO service handler.
         */
        void run_one();

        /**
         * Run until IO service is stopped or runs out of work.
         */
        void run();

        /**
         * Post a function for execution. The function will be invoked
         * from inside run() or run_one().
         */
        void post(std::function<void()>);

        /**
         * Stop the IO service processing loop and return from run_one()
         * or run() calls as soon as possible. Call to reset() is required
         * to start processing via run_one() or run() after stop() has
         * been called.
         */
        void stop();

        /**
         * Reset the IO service for subsequent call to run() or run_one().
         * This function must not be called from inside run() or run_one().
         */
        void reset();

        /**
         * Make a new socket. The underlying transport will be
         * a stream socket (TCP, SSL).
         *
         * @param uri An URI describing a desired socket scheme.
         * @param handler Pointer to socket handler implementation.
         *
         * @return Shared pointer to AsioSocket.
         */
        std::shared_ptr<AsioSocket> make_socket(
            const gu::URI& uri,
            const std::shared_ptr<AsioStreamEngine>& engine = nullptr);

        /**
         * Make a new datagram socket. The underlying transport
         * will be a datagram socket (UDP).
         *
         * @param uri An URI describing a desired socket scheme.
         * @param handler Pointer to socket handler implementation.
         *
         * @return Shared pointer to AsioDatagramSocket.
         */
        std::shared_ptr<AsioDatagramSocket> make_datagram_socket(
            const gu::URI& uri);

        /**
         * Make a new acceptor.
         *
         * @param uri Uri describing a desired socket scheme.
         * @param acceptor_handler Pointer to acceptor handler implementation.
         * @param socket_handler Pointer to socket handler implementation.
         *
         * @return Shared pointer to AsioSocketAcceptor.
         */
        std::shared_ptr<AsioAcceptor> make_acceptor(const gu::URI& uri);

        class Impl;
        Impl& impl();
    private:
        std::unique_ptr<Impl> impl_;
        const gu::Config& conf_;
    };

    class AsioSteadyTimerHandler
    {
    public:
        virtual ~AsioSteadyTimerHandler() { }
        virtual void handle_wait(const AsioErrorCode&) = 0;
    };

#if (__GNUC__ == 4 && __GNUC_MINOR__ == 4)
    typedef std::chrono::monotonic_clock AsioClock;
#else
    typedef std::chrono::steady_clock AsioClock;
#endif // (__GNUC__ == 4 && __GNUC_MINOR__ == 4)

    class AsioSteadyTimer
    {
    public:
        AsioSteadyTimer(AsioIoService& io_service);
        ~AsioSteadyTimer();
        AsioSteadyTimer(const AsioSteadyTimer&) = delete;
        AsioSteadyTimer& operator=(const AsioSteadyTimer&) = delete;
        void expires_from_now(const AsioClock::duration&);
        void async_wait(const std::shared_ptr<AsioSteadyTimerHandler>&);
        void cancel();
    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif // GU_ASIO_HPP
