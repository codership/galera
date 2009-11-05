/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id:$
 */

/*!
 * @file Transport based on gu::network::Socket
 */

#ifndef GCOMM_SOCKET_HPP
#define GCOMM_SOCKET_HPP

#include "gcomm/transport.hpp"

#include "gu_network.hpp"

namespace gcomm
{
    class Socket;
}

class gcomm::Socket : public Transport
{
public:
    Socket(Protonet& net_, const gu::URI& uri_) : 
        Transport(net_, uri_),
        socket(0) 
    { }
    
    ~Socket()
    {
        if (socket != 0)
        {
            if (socket->get_state() != gu::net::Socket::S_CLOSED)
            {
                socket->close();
            }
            socket->release();
        }
        socket = 0;
    }
    
    void connect() 
    { 
        socket = pnet.get_net().connect(uri.to_string()); 
    }

    void close() 
    { 
        socket->close(); 
        delete socket;
        socket = 0;
    }
    
    void listen() 
    { 
        socket = pnet.get_net().listen(uri.to_string()); 
    }

    Transport* accept() 
    { 
        gu::net::Socket* acc(socket->accept());
        return new Socket(pnet, acc);
    } 
    
    int handle_down(const gu::net::Datagram& dg, const ProtoDownMeta& dm)
    {
        return socket->send(&dg);
    }
    
    void handle_up(int fd, const gu::net::Datagram& dg, const ProtoUpMeta& um)
    {
        send_up(dg, um);
    }

    size_t get_mtu() const { return socket->get_mtu(); }

    std::string get_local_addr() const { return socket->get_local_addr(); }
    
    std::string get_remote_addr() const { return socket->get_remote_addr(); }
    
    int get_fd() const { return socket->get_fd(); }

    State get_state() const
    {
        if (socket == 0)
        {
            return S_CLOSED;
        }
        switch (socket->get_state())
        {
        case gu::net::Socket::S_CLOSED:
            return S_CLOSED;
        case gu::net::Socket::S_CONNECTING:
            return S_CONNECTING;
        case gu::net::Socket::S_CONNECTED:
            return S_CONNECTED;
        case gu::net::Socket::S_LISTENING:
            return S_LISTENING;
        case gu::net::Socket::S_FAILED:
            return S_FAILED;
        case gu::net::Socket::S_MAX:
            gu_throw_fatal;
        }
        gu_throw_fatal;
        throw;
    }

private:
    Socket(const Socket&);
    void operator=(const Socket&);
    
    Socket(Protonet& net_, gu::net::Socket* socket_) :
        Transport(net_, socket_->get_remote_addr()),
        socket(socket_)
    { }

    gu::net::Socket* socket;
};


#endif // GCOMM_SOCKET_HPP
