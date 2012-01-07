//
// Copyright (C) 2009 Codership Oy <info@codership.com>
//

//!
// @file socket.hpp Socket interface.
//
// This file defines socket interface used by gcomm. Currently socket interface
// provides synchronous send() but only async_recv().
//

#ifndef GCOMM_SOCKET_HPP
#define GCOMM_SOCKET_HPP

#include "gu_uri.hpp"
#include "gu_datagram.hpp"

namespace gcomm
{
    typedef const void* SocketId; //!< Socket Identifier
    class Socket;                 //!< Socket interface
    typedef boost::shared_ptr<Socket> SocketPtr;
    class Acceptor;               //!< Acceptor interfacemat
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

    /*!
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

    virtual int send(const gu::Datagram& dg) = 0;
    virtual void async_receive() = 0;

    virtual size_t get_mtu() const = 0;
    virtual std::string get_local_addr() const = 0;
    virtual std::string get_remote_addr() const = 0;
    virtual State get_state() const = 0;
    virtual SocketId get_id() const = 0;
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
    virtual State get_state() const = 0;
    virtual SocketPtr accept() = 0;
    virtual SocketId get_id() const = 0;
protected:
    const gu::URI uri_;
};

#endif // GCOMM_SOCKET_HPP
