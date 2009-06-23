/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id:$
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
 *
 * // Network context 
 * Network net;
 *
 * // Create new socket
 * Socket* sock = net.socket();
 * // Connect socket, URL without scheme implies TCP/IP (v4 or v6) 
 * sock->connect("localhost:2112");
 * 
 * // Main loop
 * bool done = false;
 * do
 * {
 *     try
 *     {
 *         const Datagram* dgram;
 *         // Wait until network event or InterruptedException
 *         const NetworkEventList& el = net.wait_event(-1);
 *         // Loop over event list (obviously this one contains only one socket)
 *         for (NetworkEventList::iterator i = el.begin(); i != el.end(); ++i)
 *         {
 *             switch (i->get_type())
 *             {
 *             case E_IN:
 *                 if ((dgram = i->get_socket()->recv()) != 0)
 *                 {
 *                     // Complete datagram received
 *                     // Do something 
 *                 }
 *                 break;
 *             case E_OUT:
 *                 // There were some bytes pending from send operation
 *                 i->get_socket()->send();
 *                 break;
 *             case E_CLOSED:
 *                 // Cleanups/error handling
 *                 done = true;
 *                 break;
 *             }
 *         }
 *     }
 *     catch (Exception e)
 *     {
 *         // Handle exceptions
 *     }
 * }
 * while (done == false);
 *
 * // Close socket
 * sock->close();
 * // Delete socket, this clears also references from Network object used 
 * // to create this socket
 * delete sock; 
 *
 * @endcode
 *
 * @author Teemu Ollakka
 *
 */

#ifndef __GU_NETWORK_HPP__
#define __GU_NETWORK_HPP__

/*! 
 * @typedef @brief Byte buffer type
 */
typedef unsigned char byte_t;

/*! 
 * @brief  Datagram container
 *
 * Datagram class provides consistent interface for managing 
 * datagrams/byte buffers. 
 */
class Datagram
{
    byte_t* buf; /*!< Private byte buffer */
    size_t buflen; /*!< Length of byte buffer */
public:
    
    /*! 
     * @brief Construct new datagram from byte buffer
     *
     * @param buf Const pointer to data buffer
     * @param buflen Length of data buffer
     */
    Datagram(const byte_t* buf, size_t buflen);
    
    /*! 
     * @brief Destruct datagram
     */
    ~Datagram();
    
    /*!
     * @brief Get const pointer to data buffer
     *
     * @param[in] offset Optional offset from the beginning of buffer (default 0)
     *
     * @return Const pointer to byte buffer at given offset
     *
     * @throws std::out_of_range If offset is greater than buffer length
     */
    const byte_t* get_buf(size_t offset = 0) const;
    
    /*!
     * @brief Get length of data buffer
     *
     * @param[in] offset Optional offset from the beginning of buffer (default 0)
     *
     * @return Length of data buffer (starting from offset)
     *
     * @throws std::out_of_range If offset is greater than buffer length
     */
    size_t get_buflen(size_t offset = 0) const;
};

/* Socket needs Network forward declaration */
class Network;

/*!
 * @brief Socket interface
 */
class Socket
{
    int fd;             /*!< Socket file descriptor                */
    int err_no;         /*!< Error number for last error           */
    int options;        /*!< Bitfield for general socket options   */
    sockaddr local_sa;  /*!< Socket address for local endpoint     */
    sockaddr remote_sa; /*!< Socket address for remote endpoint    */
    size_t sa_size;     /*!< Size of socket address                */
    Network* net;       /*!< Network object this socket belongs to */
public:
    /*!
     * Socket options
     */
    enum
    {
        O_NON_BLOCK    = 1 << 0, /*!< Socket operations are non-blocking */
        O_NO_INTERRUPT = 1 << 1  /*!< Socket methods calls are not interruptible */
    };
    
    /*!
     * @brief Default constructor
     */
    Socket();
    
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
     * @throws std::runtime_error If socket was blocking and connect failed
     */
    void connect(const string& addr);

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
    void listen(const string& addr, int backlog = 16);

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
     * @param[in] Optional flags for underlying system recv call (default none)
     *
     * @return Const pointer to Datagram object containing received datagram or
     *         0 if no complete datagram was received.
     *
     * @throws InterruptedException If underlying system call was interrupted
     *         by signal and socket option O_NO_INTERRUPT was not set
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
};

/*!
 * @brief Network event class
 */
class NetworkEvent
{
public:
    enum Type
    {
        E_IN,        /*!< Input event, socket is readable            */
        E_OUT,       /*!< Output event, socket is writable           */
        E_CONNECTED, /*!< Socket was connected (non-blocking)        */
        E_CLOSED     /*!< Socket was closed or error leading to socket close was encountered */
    };
private:    
    Type type;                   /*!< Event type              */
    Socket* socket;              /*!< Socket related to event */
    friend class Network;        
    NetworkEvent(Type, Socket*); /*!< Private constructor */
public:    
    /*!
     * @brief Get event type
     *
     * @return Event type
     */
    Type get_type() const;
    
    /*!
     * @brief Get pointer to corresponding socket
     *
     * @return Pointer to socket object
     */
    Socket* get_socket() const;
};

/*!
 * @brief List of network events
 *
 */
class NetworkEventList
{
public:
    /*
     * TODO: Define iterators
     */

    /*!
     * @brief Get iterator to the beginning of network event list
     */
    const_iterator begin() const;
    
    /*!
     * @brief Get iterator to the end (one past the last) of network 
     *        event list
     */
    const_iterator end() const;
    
    /*!
     * @brief Get size of network event list 
     */
    size_t size() const;
};

/*!
 * @brief Network interface
 */
class Network
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
    
    /*!
     * @brief Create new socket
     *
     * Creates new Socket and returns pointer the object. Note that 
     * there is no need to specify socket type at this point, 
     * it is deduced from address URL at connect/accept/listen call.
     *
     * @return Pointer to new socket
     */
    Socket* socket();

    /*!
     * @brief Wait network events
     *
     * Poll for network events until timeout expires.
     *
     * @return Const reference for network event list
     *
     * @throws InterruptedException If underlying system call
     *         was interrupted by signal
     * @throws std::runtime_error If error was encountered
     */
    const NetworkEventList& wait_event(long timeout = 0);
};
