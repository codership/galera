/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

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

/* size_t */
#include <cstdlib>
/* sockaddr */
#include <sys/socket.h>

#include <string>
#include <vector>

namespace gu
{

    namespace net
    {
        /*! 
         * @typedef @brief Byte buffer type
         */
        typedef unsigned char byte_t;
        typedef std::vector<byte_t> Buffer;
        class Datagram;
        class Socket;
        class SocketList;
        class EPoll;
        class NetworkEvent;
        class Network;
        
        namespace URLScheme
        {
            static const std::string tcp = "tcp";
        }
        
        
        int closefd(int fd);
    }
}




/*! 
 * @brief  Datagram container
 *
 * Datagram class provides consistent interface for managing 
 * datagrams/byte buffers. 
 */
class gu::net::Datagram
{
    Buffer header;
    Buffer payload;
    size_t offset;
    /* Disallow assignment, for copying use copy constructor */
    
    // void operator=(const Datagram&);
public:
    /*! 
     * @brief Construct new datagram from byte buffer
     *
     * @param[in] buf Const pointer to data buffer
     * @param[in] buflen Length of data buffer
     *
     * @throws std::bad_alloc 
     */
    Datagram(const Buffer& buf_, size_t offset_ = 0);
    
    /*!
     * @brief Copy constructor
     *
     * @param[in] dgram Datagram to make copy from
     *
     * @throws std::bad_alloc
     */
    Datagram(const Datagram& dgram);

    
    /*! 
     * @brief Destruct datagram
     */
    ~Datagram();
    
    Buffer& get_header() { return header; }
    const Buffer& get_header() const { return header; }
    Buffer& get_payload() { return payload; }
    const Buffer& get_payload() const { return payload; }
    size_t get_len() const { return (header.size() + payload.size()); }
    size_t get_offset() const { return offset; }
};




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
private:
    /* Private data */
    int fd;             /*!< Socket file descriptor                */
    int err_no;         /*!< Error number for last error           */
    int options;        /*!< Bitfield for general socket options   */
    int event_mask;     /*!< Bitfield for waited network events    */
    sockaddr local_sa;  /*!< Socket address for local endpoint     */
    sockaddr remote_sa; /*!< Socket address for remote endpoint    */
    socklen_t sa_size;     /*!< Size of socket address                */
    
    static const size_t hdrlen = sizeof(uint32_t);
    size_t mtu;
    size_t max_pending;

    size_t dgram_offset; /*!< Offset of the last read datagram  */
    bool complete;     /*!< Boolean denoting that compleme dgram
                        * is waiting to be read */

    Buffer recv_buf;  /*!< Buffer for received data        */
    size_t recv_buf_offset;
    Datagram dgram;       /*!< Datagram container               */
    Buffer pending;  /*!< Buffer for pending outgoing data */
    State state;        /*!< Socket state                       */
    
    /* Network integration */
    friend class Network;
    friend class EPoll;
    Network& net;       /*!< Network object this socket belongs to */
    
    /* Private methods */
    
    /*!
     * @brief Constructor
     */
    Socket(Network& net,
           const int fd = -1,
           const int options = (O_NO_INTERRUPT),
           const sockaddr* local_sa = 0, 
           const sockaddr* remote_sa = 0,
           const socklen_t sa_size = 0,
           const size_t mtu = (1 << 15),
           const size_t max_pending = (1 << 20));
        
    /*!
     * @brief Change socket state
     */
    void set_state(State, int err = 0);
    
    /*!
     * @brief Open new socket
     *
     * @param[in] addr Address URL
     *
     * @throws std::runtime_error If address could not be resolved or 
     *         socket could not be created
     */
    void open_socket(const std::string& addr);
    
    Socket(const Socket&);
    void operator=(const Socket&);
    
public:
    /*!
     * @brief Get file descriptor corresponding to socket.
     *
     * @note This method should not be used except for testing.
     */
    int get_fd() const
    {
        return fd;
    }
private:
    
    /*!
     * @brief Get current event mask for socket.
     */
    void set_event_mask(const int m)
    {
        event_mask = m;
    }

    /*!
     * @brief Set event mask for socket.
     */
    int get_event_mask() const
    {
        return event_mask;
    }

    /*!
     * @brief Get max pending bytes
     */
    size_t get_max_pending_len() const;

    /*!
     * @brief Send pending bytes
     */
    int send_pending(int);
public:
    /*!
     * Socket options
     */
    enum
    {
        /* NOTE: O_NON_BLOCKING is needed for non-blocking connect() */
        O_NON_BLOCKING = 1 << 0, /*!< Socket operations are non-blocking */
        O_NO_INTERRUPT = 1 << 1  /*!< Socket methods calls are not 
                                  * interruptible */
    };
        
    /*!
     * @brief Destructor
     */
    ~Socket();
    
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
     * @throws std::logic_error If socket was not open (connected, listening or accepted)
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
     * @brief Set socket options
     *
     * @throws std::invalid_argument
     */
    void setopt(int opts);

    /*!
     * @brief Get socket options
     *
     * @return Socket options mask
     */
    int getopt() const;

    /*!
     * @brief Get socket state
     *
     * @return Current state of the socket
     */
    State get_state() const;

    /*!
     * @brief Get errno corresponding to last error
     *
     * @return Error number corresponding to the last error
     */
    int get_errno() const;

    /*!
     * @brief Get human readable error string corresponding to last error
     *
     * @return Error string
     */
    const std::string get_errstr() const;
};

/*!
 * @brief Network event class
 */
class gu::net::NetworkEvent
{
public:
    /*!
     * @brief Network event type enumeration
     */
    enum
    {
        E_IN        = 1 << 0, /*!< Input event, socket is readable */
        E_OUT       = 1 << 1, /*!< Output event, socket is writable */
        E_ACCEPTED  = 1 << 2, /*!< New connection has been accepted */
        
        E_CONNECTED = 1 << 3, /*!< Socket connect was completed 
                                (non-blocking socket)*/
        E_ERROR = 1 << 4,    /*!< Socket was closed or error leading to 
                               socket close was encountered */
        E_CLOSED = 1 << 5,
        E_EMPTY = 1 << 6
    };
private:    
    int event_mask;             /*!< Event mask              */
    Socket* socket;             /*!< Socket related to event */
    friend class Network;        
    NetworkEvent(int, Socket*); /*!< Private constructor */
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
};



/*!
 * @brief Network interface
 */
class gu::net::Network
{
    friend class Socket;
    SocketList* sockets;
    int wake_fd[2];
    EPoll* poll;
    void insert(Socket*);
    void erase(Socket*);
    Socket* find(int);
    /* Don't allow assignment or copy construction */
    Network operator=(const Network&);
    Network(const Network&);
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
    NetworkEvent wait_event(int timeout = -1);
    
    /**
     *
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
    
};

#endif /* __GU_NETWORK_HPP__ */
