//
// Copyright (C) 2009-2019 Codership Oy <info@codership.com>
//

//!
// @file socket.hpp Socket interface.
//
// This file defines socket interface used by gcomm. Currently socket interface
// provides synchronous send() but only async_recv().
//

#ifndef GCOMM_SOCKET_HPP
#define GCOMM_SOCKET_HPP

#include "gcomm/datagram.hpp"

#include "gu_uri.hpp"


namespace gcomm
{
    typedef const void* SocketId; //!< Socket Identifier
    class Socket;                 //!< Socket interface
    typedef std::shared_ptr<Socket> SocketPtr;
    class Acceptor;               //!< Acceptor interfacemat

    /**
     * Statistics for socket connection. Currently relevant only
     * to TCP stream sockets and available on Linux/FreeBSD only.
     */
    typedef struct socket_stats_st
    {
        /* Stats from kernel - tcp_info for TCP sockets. */
        long rtt;     /** RTT in usecs. */
        long rttvar;  /** RTT variance in usecs. */
        long rto;     /** Retransmission timeout in usecs. */
        long lost;    /** Estimate of lost packets (Linux only). */
        long last_data_recv; /** Time since last received data in msecs. */
        long cwnd; /** Congestion window */
        /* Stats from userspace */
        long last_queued_since;    /** Last queued since in msecs           */
        long last_delivered_since; /** Last delivered since in msecs        */
        long send_queue_length;    /** Number of messaged pending for send. */
        long send_queue_bytes;     /** Number of bytes in send queue.       */
        std::vector<std::pair<int, size_t> > send_queue_segments;
        socket_stats_st() : rtt(), rttvar(), rto(), lost(), last_data_recv(),
                            cwnd(),
                            last_queued_since(),
                            last_delivered_since(),
                            send_queue_length(),
                            send_queue_bytes(),
                            send_queue_segments()
        { }
    } SocketStats;
    static inline
    std::ostream& operator<<(std::ostream& os,
                             const SocketStats& stats)
    {
        os << "rtt: " << stats.rtt
           << " rttvar: " << stats.rttvar
           << " rto: " << stats.rto
           << " lost: " << stats.lost
           << " last_data_recv: " << stats.last_data_recv
           << " cwnd: " << stats.cwnd
           << " last_queued_since: " << stats.last_queued_since
           << " last_delivered_since: " << stats.last_delivered_since
           << " send_queue_length: " << stats.send_queue_length
           << " send_queue_bytes: " << stats.send_queue_bytes;
        for (std::vector<std::pair<int, size_t> >::const_iterator i(stats.send_queue_segments.begin()); i != stats.send_queue_segments.end(); ++i)
        {
            os << " segment: " << i->first << " messages: " << i->second;
        }
        return os;
    }
}



class gcomm::Socket
{
public:
    typedef enum
    {
        S_CLOSED,
        S_CONNECTING,
        S_CONNECTED,
        S_FAILED,
        S_CLOSING
    } State;

    /**
     * Symbolic option names (to specify in URI)
     */
    static const std::string OptNonBlocking; /*! socket.non_blocking */
    static const std::string OptIfAddr;      /*! socket.if_addr      */
    static const std::string OptIfLoop;      /*! socket.if_loop      */
    static const std::string OptCRC32;       /*! socket.crc32        */
    static const std::string OptMcastTTL;    /*! socket.mcast_ttl    */

    Socket(const gu::URI& uri)
        :
        uri_(uri)
    { }

    virtual ~Socket() { }
    virtual void connect(const gu::URI& uri) = 0;
    virtual void close() = 0;

    virtual void set_option(const std::string& key, const std::string& val) = 0;
    // Send a datagram originating from segment. The segment parameter
    // can be used by the implementation to implement fair queuing for
    // messages originating from different segments.
    virtual int send(int segment, const Datagram& dg) = 0;
    virtual void async_receive() = 0;

    virtual size_t mtu() const = 0;
    virtual std::string local_addr() const = 0;
    virtual std::string remote_addr() const = 0;
    virtual State state() const = 0;
    virtual SocketId id() const = 0;
    virtual SocketStats stats() const = 0;
protected:
    const gu::URI uri_;
};

class gcomm::Acceptor
{
public:
    typedef enum
    {
        S_CLOSED,
        S_LISTENING,
        S_FAILED
    } State;

    Acceptor(const gu::URI& uri)
        :
        uri_(uri)
    { }

    virtual ~Acceptor() { }

    virtual void listen(const gu::URI& uri) = 0;
    virtual std::string listen_addr() const = 0;
    virtual void close() = 0;
    virtual State state() const = 0;
    virtual SocketPtr accept() = 0;
    virtual SocketId id() const = 0;
protected:
    const gu::URI uri_;
};

#endif // GCOMM_SOCKET_HPP
