
// gu::Network based implementation

#ifndef GCOMM_GU_HPP
#define GCOMM_GU_HPP

#include "gu_network.hpp"
#include "gu_lock.hpp"
#include "socket.hpp"
#include "gcomm/protonet.hpp"
#include "gcomm/exception.hpp"

#include <deque>

namespace gcomm
{
    class GuProtonet;
    class GuSocket;
    class GuAcceptor;
}





class gcomm::GuProtonet : public Protonet
{
public:
    GuProtonet() : Protonet("gu"), net(), mutex(), interrupted(false) { }
    ~GuProtonet() { }
    gu::net::Network& get_net() { return net; }
    void insert(Protostack* pstack);
    void erase(Protostack* pstack);
    SocketPtr socket(const gu::URI& uri);
    gcomm::Acceptor* acceptor(const gu::URI& uri);

    void event_loop(const gu::datetime::Period&);
    void interrupt()
    {
        gu::Lock lock(mutex);
        interrupted = true;
        net.interrupt();
    }
    gu::Mutex& get_mutex() { return mutex; }
    void enter() { mutex.lock(); }
    void leave() { mutex.unlock(); }
private:
    GuProtonet(const GuProtonet&);
    void operator=(const GuProtonet&);
    gu::net::Network net;
    gu::Mutex mutex;
    bool interrupted;
};



class gcomm::GuSocket : public gcomm::Socket
{
public:
    GuSocket(GuProtonet& net, const gu::URI& uri) : 
        Socket(uri),
        net_(net),
        socket_(0) 
    { }
    
    ~GuSocket()
    {
        if (socket_ != 0)
        {
            if (socket_->get_state() != gu::net::Socket::S_CLOSED)
            {
                socket_->close();
            }
            socket_->release();
        }
        socket_ = 0;
    }
    
    void connect(const gu::URI& uri) 
    { 
        gcomm_assert(uri.get_scheme() == scheme_);
        socket_ = net_.get_net().connect(uri.to_string()); 
        gcomm_assert((socket_->get_opt() & gu::net::Socket::O_NON_BLOCKING) != 0);
    }
    
    void close() 
    { 
        if (socket_ != 0)
        {
            socket_->close(); 
        }
        else
        {
            log_debug << "closing unopened socket";
        }
        delete socket_;
        socket_ = 0;
    }
    
    int send(const gu::Datagram& dg)
    {
        return socket_->send(&dg);
    }
    
    void async_receive()
    {
        net_.get_net().set_event_mask(socket_, gu::net::E_IN);
    }

    size_t get_mtu() const { return socket_->get_mtu(); }

    std::string get_local_addr() const { return socket_->get_local_addr(); }
    
    std::string get_remote_addr() const { return socket_->get_remote_addr(); }
    
    State get_state() const
    {
        if (socket_ == 0)
        {
            return S_CLOSED;
        }
        switch (socket_->get_state())
        {
        case gu::net::Socket::S_CLOSED:
            return S_CLOSED;
        case gu::net::Socket::S_CONNECTING:
            return S_CONNECTING;
        case gu::net::Socket::S_CONNECTED:
            return S_CONNECTED;
        case gu::net::Socket::S_FAILED:
            return S_FAILED;
        default:
            gu_throw_fatal;
            throw;
        }
    }
    SocketId get_id() const { return socket_; }
private:
    GuSocket(const GuSocket&);
    void operator=(const GuSocket&);
    
    friend class gcomm::GuAcceptor;
    GuSocket(GuProtonet& net, gu::net::Socket* socket) :
        Socket(socket->get_remote_addr()),
        net_(net),
        socket_(socket)
    { }
    
    GuProtonet& net_;
    gu::net::Socket* socket_;
};

class gcomm::GuAcceptor : public Acceptor
{
public:

    GuAcceptor(GuProtonet& net, const gu::URI& uri)
        :
        Acceptor(uri),
        net_(net),
        socket_(0)
    { }

    void listen(const gu::URI& uri) 
    { 
        gcomm_assert(uri.get_scheme() == scheme_);
        socket_ = net_.get_net().listen(uri.to_string()); 
    }
    
    SocketPtr accept() 
    { 
        gu::net::Socket* acc(socket_->accept());
        gcomm_assert((acc->get_opt() & gu::net::Socket::O_NON_BLOCKING) != 0);
        return SocketPtr(new GuSocket(net_, acc));
    } 
    
    State get_state() const
    {
        if (socket_ == 0)
        {
            return S_CLOSED;
        }
        switch (socket_->get_state())
        {
        case gu::net::Socket::S_CLOSED:
            return S_CLOSED;
        case gu::net::Socket::S_LISTENING:
            return S_LISTENING;
        case gu::net::Socket::S_FAILED:
            return S_FAILED;
        default:
            gu_throw_fatal;
            throw;
        }
    }

    
    void close() 
    { 
        if (socket_ != 0)
        {
            socket_->close(); 
        }
        else
        {
            log_debug << "closing unopened socket";
        }
        delete socket_;
        socket_ = 0;
    }

    SocketId get_id() const { return socket_; }
    
private:
    GuAcceptor(const GuAcceptor&);
    void operator=(const GuAcceptor&);
    GuProtonet& net_;
    gu::net::Socket* socket_;
};


#endif // GCOMM_GU_HPP
