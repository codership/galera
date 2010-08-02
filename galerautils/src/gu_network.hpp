// Copyright (C) 2009 Codership Oy <info@codership.com>

/*!
 * @file gu_network.hpp Network API
 *
 * These classes provide simple API for datagram oriented network
 * communication. Because datagram-like communication emulation over stream
 * channels is provided, connectivity over this interface may
 * not be transparent/compatible with communication over standard
 * socket interface. This is the case especially with TCP.
 *
 * If fully transparet stream communication is desired, library
 * must be extended to work with Stream class (along with Datagram).
 *
 * All addresses are represented in human readable URL like strings.
 *
 * Example usage:
 *
 * @code
 * // TODO: Client exampl
 * @endcode
 *
 * @code
 * // TODO: Server example
 * @endcode
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 *
 */

#ifndef __GU_NETWORK_HPP__
#define __GU_NETWORK_HPP__

#include "gu_buffer.hpp"
#include "gu_datagram.hpp"
#include "gu_poll.hpp"
#include "gu_datetime.hpp"
#include "gu_exception.hpp"
#include <cstdlib>      /* size_t */
#include <stdint.h>     /* uint32_t */
#include <sys/socket.h> /* sockaddr */

#include <string>
#include <vector>
#include <limits>
#include <cassert>

// Forward declarations
namespace gu
{
    class URI;
    namespace net
    {
        class Sockaddr;
        class Addrinfo;
    }
}

// Declarations in this header
namespace gu
{
    namespace net
    {
        class Socket;
        std::ostream& operator<<(std::ostream&, const Socket&);
        class SocketList;
        class EPoll;
        class NetworkEvent;
        class Network;
        static const size_t default_mtu = (1 << 15);
        namespace URLScheme
        {
            static const std::string tcp = "tcp";
        }
        int closefd(int);
    }
}


/*!
 * @brief Socket interface
 */
class gu::net::Socket
{
public:
    /* Public enumerations and typedefs */
    /*!
     * @brief Socket states
     */
    enum State
    {
        S_CLOSED,      /*!< Closed */
        S_CONNECTING,  /*!< Non-blocking socket is connecting */
        S_CONNECTED,   /*!< Socket is connected */
        S_LISTENING,   /*!< Socket is listening for connections */
        S_FAILED,      /*!< Socket is in failed state but not closed yet */
        S_MAX
    };

    /*!
     * Socket options
     */
    enum SockOpt
    {
        /* NOTE: O_NON_BLOCKING is needed for non-blocking connect() */
        O_NON_BLOCKING = 1 << 0, /*!< Socket operations are non-blocking */
        O_NO_INTERRUPT = 1 << 1, /*!< Socket methods calls are not
                                  *   interruptible */
        O_CRC32        = 1 << 2  /*!< Add crc32 in outgoing datagrams */
    };

    int get_opt() const { return options; }

    /*!
     * Symbolic option names (to specify in URI)
     */
    static const std::string OptNonBlocking; /*! socket.non_blocking */
    static const std::string OptIfAddr;      /*! socket.if_addr      */
    static const std::string OptIfLoop;      /*! socket.if_loop      */
    static const std::string OptCRC32;       /*! socket.crc32        */
    static const std::string OptMcastTTL;    /*! socket.mcast_ttl    */

    /*!
     * @brief Get file descriptor corresponding to socket.
     *
     * @note This method should not be used except for testing.
     */
    int get_fd() const { return fd; }

    /*!
     * @brief Connect socket
     *
     * Connect to location given in @p addr.
     *
     * @param[in] addr Address URL to make connection to
     *
     * @throws std::invalid_argument If @p addr URL was not valid
     * @throws std::runtime_error If error was encountered in socket creation
     * @throws std::runtime_error If socket was blocking and connect failed
     */
    void connect(const std::string& addr);

    /*!
     * @brief Close socket
     *
     * Close socket (more about possible side effects here)
     *
     * @throws std::logic_error If socket was not open (connected, listening
     *                          or accepted)
     */
    void close();

    /*!
     * @brief Start listening on @p addr
     *
     * @param[in] addr Address URL to bind and start listening to
     * @param[in] backlog Backlog parameter passed to underlying listen call
     *            (defaults 16)
     */
    void listen(const std::string& addr, int backlog = 16);

    /*!
     * @brief Accept new connection
     *
     * @return New connected socket
     *
     * @throws std::logic_error If accept is called for socket that is
     *         not listening
     */
    Socket* accept();

    /**
     * @brief Receive complete datagram from socket.
     *
     * @param[in] flags Optional flags for underlying system recv call
     *            (default none)
     *
     * @return Const pointer to Datagram object containing received
     *         datagram or 0 if no complete datagram was received.
     *
     * @throws InterruptedException If underlying system call was
     *         interrupted by signal and socket option O_NO_INTERRUPT
     *         was not set
     * @throws std::runtime_error If other error was encountered during call
     */
    const Datagram* recv(int flags = 0);

    /**
     * @brief Send complete datagram
     *
     * Send complete datagram over socket connection. Whole datagram
     * is sent or scheduled to be sent when this call returs. However,
     * note that if the socket is made non-blocking, some parts of the
     * datagram may still be scheduled for sending. If Network.wait_event()
     * returns event indicating that this socket has become writable again,
     * @f send() must be called to schedule resending of remaining bytes.
     *
     *
     * @param[in] dgram Const pointer to datagram to be send
     * @param[in] flags Optional flags for send call (default none)
     *
     * @return Zero if send call was successfull or error number (EAGAIN) in
     *         case of error
     *
     * @throws InterruptedException if underlying system call was
     *         interrupted by signal and socket option O_NO_INTERRUPT was
     *         not set
     * @throws std::runtime_error If other error was encountered
     */
    int send(const Datagram* dgram = 0, int flags = 0);

    /*!
     * @brief Get socket state
     *
     * @return Current state of the socket
     */
    State get_state() const { return state; }

    /*!
     * @brief Get errno corresponding to last error
     *
     * @return Error number corresponding to the last error
     */
    int get_errno() const { return err_no; }

    /*!
     * @brief Get human readable error string corresponding to last error
     *
     * @return Error string
     */
    const std::string get_errstr() const { return ::strerror(err_no); }

    std::string get_local_addr()  const { return local_addr;  }
    std::string get_remote_addr() const { return remote_addr; }
    size_t      get_mtu()         const { return mtu; }

    bool has_unread_data() const
    {
        return (recv_buf_offset > 0 &&
                recv_buf_offset == dgram.get_payload().size());
    }

    void release();

    /*!
     * @brief Destructor
     */
    ~Socket();



private:
    friend std::ostream& operator<<(std::ostream&, const Socket&);

    /* Private data */
    int fd;             /*!< Socket file descriptor                 */
    int err_no;         /*!< Error number for last error            */
    int options;        /*!< Bitfield for general socket options    */
    int event_mask;     /*!< Bitfield for waited network events     */
    Addrinfo* listener_ai;
    Sockaddr* sendto_addr; // Needed for dgram sockets

    std::string local_addr;
    std::string remote_addr;

    size_t mtu;             // For outgoing data
    size_t max_packet_size; // For incoming date
    size_t max_pending;

    gu::Buffer recv_buf;    /*!< Buffer for received data         */
    size_t recv_buf_offset; /*! Offset to the end of read data    */
    gu::Datagram dgram;     /*!< Datagram container               */
    gu::Buffer pending;     /*!< Buffer for pending outgoing data */
    State state;            /*!< Socket state                     */

    /* Network integration */
    friend class Network;
    Network& net;       /*!< Network object this socket belongs to */

    /* Private methods */

    /*!
     * @brief Constructor
     */
    Socket(Network& net,
           const int fd = -1,
           const std::string& local_addr = "",
           const std::string& remote_addr = "",
           const size_t mtu = default_mtu,
           const size_t max_packet_size = default_mtu,
           const size_t max_pending = default_mtu*5);

    /*!
     * @brief Change socket state
     */
    void set_state(State, int err = 0);

    void*     get_sendto_addr() const;
    socklen_t get_sendto_addr_len() const;

    Socket(const Socket&);
    void operator=(const Socket&);

private:

    /*! @brief Set options before connect */
    static void set_opt(Socket*, const Addrinfo&, int opt);

    /*!
     * @brief Set current event mask for socket.
     */
    void set_event_mask(const int m) { event_mask = m; }

    /*!
     * @brief Get event mask for socket.
     */
    int get_event_mask() const { return event_mask; }

    /*!
     * @brief Get max pending bytes
     */
    size_t get_max_pending_len() const;

    /*!
     * @brief Send pending bytes
     */
    int send_pending(int);
};

/*!
 * @brief Network event class
 */
class gu::net::NetworkEvent
{
public:
    /*!
     * @brief Get event type
     *
     * @return Event type
     */
    int get_event_mask() const;

    /*!
     * @brief Get pointer to corresponding socket
     *
     * @return Pointer to socket object
     */
    Socket* get_socket() const;

private:
    int event_mask;             /*!< Event mask              */
    Socket* socket;             /*!< Socket related to event */
    friend class Network;
    NetworkEvent(int, Socket*); /*!< Private constructor     */
};


/*!
 * @brief Network interface
 */
class gu::net::Network
{
public:

    /*!
     * @brief Default constructor
     */
    Network();

    /*!
     * @brief Destructor
     */
    ~Network();

    Socket* connect(const std::string& addr);
    Socket* listen(const std::string& addr);

    /*!
     * @brief Wait network event
     *
     * Poll for network events until timeout expires.
     *
     * @param timeout Timeout after which waiting is interrupted
     *
     * @return Network event
     *
     * @throws InterruptedException If underlying system call
     *         was interrupted by signal
     * @throws std::runtime_error If error was encountered
     */
    NetworkEvent wait_event(const gu::datetime::Period& =
                                  gu::datetime::Period(-1),
                            bool auto_handle = true);

    /*!
     * Interrupt network wait_event()
     */
    void interrupt();

    /*!
     * @brief Set event mask for @p sock
     *
     * @param[in] sock Socket to set mask for
     * @param[in] mask Event mask
     *
     * @throws std::invalid_argument If event mask was invalid
     * @throws std::logic_error If socket was not found or was in
     *         invalid state
     */
    void set_event_mask(Socket* sock, int mask);

    /*!
     *
     */
    static size_t get_mtu() { return default_mtu; }

private:

    friend class Socket;
    SocketList* sockets;
    std::vector<Socket*> released;
    int wake_fd[2];
    Poll* poll;
    void insert(Socket*);
    void erase(Socket*);
    void release(Socket*);
    Socket* find(int);
    /* Don't allow assignment or copy construction */
    Network operator=(const Network&);
    Network(const Network&);
};

#endif /* __GU_NETWORK_HPP__ */
