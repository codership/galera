/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/* Galerautil C++ includes */
#include "gu_network.hpp"
#include "gu_resolver.hpp"
#include "gu_uri.hpp"
#include "gu_logger.hpp"
#include "gu_epoll.hpp"
#include "gu_serialize.hpp"


/* C-includes */
#include <cstring>
#include <cerrno>
#include <cassert>

/* STD/STL includes */
#include <stdexcept>
#include <sstream>
#include <map>
#include <vector>
#include <algorithm>

/* System includes */
#include <fcntl.h>         // fcntl() to set O_NONBLOCK
#include <netinet/tcp.h>   // TCP_NODELAY

using namespace std;
using namespace gu;
using namespace gu::datetime;

static int get_opt(const URI& uri)
{
    int ret(0);
    try
    {
        bool val(from_string<bool>(uri.get_option("socket.non_blocking")));
        ret |= (val == true ? gu::net::Socket::O_NON_BLOCKING : 0);
    }
    catch (NotFound&) { }
    return ret;
}

int gu::net::closefd(int fd)
{
    int err;
    do
    {
        err = ::close(fd);
    }
    while (err == -1 && errno == EINTR);
    if (err == -1)
    {
        err = errno;
    }
    return err;
}


/**************************************************************************
 * Datagram implementation
 **************************************************************************/

gu::net::Datagram::Datagram(const Buffer& buf_, size_t offset_) :
    header  (),
    payload (new Buffer(buf_)),
    offset  (offset_)
{ }


void gu::net::Datagram::normalize()
{
    payload = boost::shared_ptr<Buffer>(new Buffer(*payload));

    if (header.size() > 0)
    {
        payload->insert(payload->begin(), header.begin(), header.end());
        header.clear();
    }

    if (offset > 0)
    {
        payload->erase(payload->begin(), payload->begin() + offset);
        offset = 0;
    }
}

/**************************************************************************
 * Socket implementation
 **************************************************************************/

gu::net::Socket::Socket(Network& net_,
                        const int fd_,
                        const string& local_addr_,
                        const string& remote_addr_,
                        const size_t mtu_,
                        const size_t max_packet_size_,
                        const size_t max_pending_) :
    fd              (fd_),
    err_no          (0),
    options         (O_NO_INTERRUPT),
    event_mask      (0),
    listener_ai     (0),
    local_addr      (local_addr_),
    remote_addr     (remote_addr_),
    mtu             (mtu_),
    max_packet_size (max_packet_size_),
    max_pending     (max_pending_),
    recv_buf        (mtu + hdrlen),
    recv_buf_offset (0),
    dgram           (),
    pending         (),
    state           (S_CLOSED),
    net             (net_)
{
    pending.reserve(max_pending);
}

gu::net::Socket::~Socket()
{
    if (fd != -1)
    {
        close();
    }
}


/* 
 * State handling
 */
void gu::net::Socket::set_state(const State s, const int err)
{
    /* Allowed state transitions */
    static const bool allowed[S_MAX][S_MAX] = {
        /* CL,   CTING, CTED,  LIS,   FAIL */
        { false, true,  true,  true,  true }, /* CL     */
        { true,  false, true,  false, true }, /* CTING  */
        { true,  false, false, false, true }, /* CTED   */
        { true,  false, false, false, true }, /* LIS    */
        { true,  false, false, false, true }  /* FAIL   */
    };

    if (allowed[get_state()][s] == false)
    {
        gu_throw_fatal << "invalid state change" << state << " -> " << s;
    }

    log_debug << "socket " << fd << " state " << state << " -> " << s; 
    state  = s;
    err_no = err;
}

gu::net::Socket::State gu::net::Socket::get_state() const
{
    return state;
}

int gu::net::Socket::get_errno() const
{
    return err_no;
}

const string gu::net::Socket::get_errstr() const
{
    return ::strerror(err_no);
}

void gu::net::Socket::set_opt(Socket* s, 
                              const Addrinfo& ai, 
                              const int opt)
{
    if (ai.get_socktype() == SOCK_STREAM)
    {
        const int no_nagle(1);

        if (::setsockopt(s->get_fd(), IPPROTO_TCP, TCP_NODELAY, &no_nagle,
                         sizeof(no_nagle)) == -1)
        {
            gu_throw_error(errno) << "setting TCP_NODELAY failed";
        }
    }
    
    if ((opt & O_NON_BLOCKING) != 0)
    {
        if (::fcntl(s->get_fd(), F_SETFL, O_NONBLOCK) == -1)
        {
            gu_throw_error(errno) << "setting fd non-blocking failed";
        }

        s->options |= O_NON_BLOCKING;
    }
}


void gu::net::Socket::connect(const string& addr)
{
    URI uri(addr);
    Addrinfo ai(resolve(uri));
    
    remote_addr = ai.to_string();
    
    if ((fd = ::socket(ai.get_family(), ai.get_socktype(), 
                       ai.get_protocol())) == -1)
    {
        gu_throw_error(errno) << "failed to open socket: " << addr;
    }
    
    set_opt(this, ai, ::get_opt(uri));
    
    Sockaddr sa(ai.get_addr());

    if (::connect(fd, &sa.get_sockaddr(), sa.get_sockaddr_len()) == -1)
    {
        if ((get_opt() & O_NON_BLOCKING) && errno == EINPROGRESS)
        {
            log_debug << "non blocking connecting";
            set_state(S_CONNECTING);
        }
        else
        {
            set_state(S_FAILED, errno);
            gu_throw_error(errno) << "connect failed: " << remote_addr;
        }
    }
    else
    {
        socklen_t sa_len(sa.get_sockaddr_len());
        if (::getsockname(fd, &sa.get_sockaddr(), &sa_len) == -1)
        {
            set_state(S_FAILED, errno);
            gu_throw_error(errno);
        }
        if (sa_len != sa.get_sockaddr_len())
        {
            set_state(S_FAILED, EINVAL);
            gu_throw_fatal << "addr len mismatch";
        }
        local_addr = Addrinfo(ai, sa).to_string();
        set_state(S_CONNECTED);
    }

    assert(get_state() == S_CONNECTING || get_state() == S_CONNECTED);
}

void gu::net::Socket::close()
{
    if (fd == -1)
    {
        log_error << "socket not open: " << fd << " " << get_state();
        gu_throw_fatal << "socket " << fd << " not open " << get_state();
    }
    
    /* Close fd only after calling net.erase() */
    net.erase(this);
    
    log_debug << "closing socket " << fd;
    int err = closefd(fd);

    if (err != 0)
    {
        set_state(S_FAILED, err);
        gu_throw_error(err) << "close failed";
    }
    
    // Recv may set state to closed when reading zero bytes from socket.
    if (get_state() != S_CLOSED)
    {
        set_state(S_CLOSED);    
    }

    delete listener_ai;
    fd = -1;
}

void gu::net::Socket::listen(const std::string& addr, const int backlog)
{
    URI uri(addr);
    Addrinfo ai(resolve(uri));
    
    Sockaddr sa(ai.get_addr());
    
    local_addr = ai.to_string();
    
    if ((fd = ::socket(ai.get_family(), ai.get_socktype(), ai.get_protocol())) == -1)
    {
        set_state(S_FAILED, errno);
        gu_throw_error(errno) << "failed to open socket";
    }
    
    const int reuse = 1;
    const socklen_t reuselen = sizeof(reuse);

    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, reuselen) == -1)
    {
        set_state(S_FAILED, errno);
        gu_throw_error(errno) << "setsockopt(SO_REUSEADDR) failed";
    }
    
    if (::bind(fd, &sa.get_sockaddr(), sa.get_sockaddr_len()) == -1)
    {
        set_state(S_FAILED, errno);
        gu_throw_error(errno) << "bind failed: " << local_addr;
    }
    
    if (::listen(fd, backlog) == -1)
    {
        set_state(S_FAILED, errno);
        gu_throw_error(errno) << "listen failed: " << local_addr;
    }

    set_opt(this, ai, ::get_opt(uri));    
    listener_ai = new Addrinfo(ai);    
    set_state(S_LISTENING);
}

gu::net::Socket* gu::net::Socket::accept()
{    
    int acc_fd;
    
    Addrinfo acc_ai(*listener_ai);
    Sockaddr acc_sa(acc_ai.get_addr());
    
    socklen_t sa_len(acc_sa.get_sockaddr_len());

    if ((acc_fd = ::accept(fd, &acc_sa.get_sockaddr(), &sa_len)) == -1)
    {
        set_state(S_FAILED, errno);
        gu_throw_error(errno);
    }
    
    if (acc_sa.get_sockaddr_len() != sa_len)
    {
        set_state(S_FAILED, EINVAL);
        gu_throw_error(EINVAL) << "sockaddr size mismatch";
    }
    
    Sockaddr local_sa(acc_sa);

    sa_len = local_sa.get_sockaddr_len();

    if (::getsockname(acc_fd, &local_sa.get_sockaddr(), &sa_len) == -1)
    {
        gu_throw_error(errno);
    }

    if (sa_len != local_sa.get_sockaddr_len())
    {
        set_state(S_FAILED, EINVAL);
        gu_throw_error(EINVAL) << "sockaddr size mismatch";
    }
    
    Socket* ret = new Socket(net, 
                             acc_fd, 
                             Addrinfo(acc_ai, local_sa).to_string(), 
                             Addrinfo(acc_ai, acc_sa).to_string());
    set_opt(ret, acc_ai, options);
    ret->set_state(S_CONNECTED);
    net.insert(ret);
    net.set_event_mask(ret, E_IN);
    return ret;
}

/*
 * Send helpers
 */

/*
 * Send contents of iov 
 *
 * - Always return number of sent bytes
 * - Param errval will contain error code if all of the bytes were not sent
 *   + sendmsg() error: corresponding errno
 *   + sendmsg() returns 0: EPIPE
 *   + sendmsg() returns < tot_len: EAGAIN
 */
static size_t iov_send(const int fd, 
                       struct iovec iov[],
                       const size_t iov_len,
                       const size_t tot_len,
                       const int flags,
                       int* errval)
{
    assert(errval != 0);
    assert(tot_len > 0);

    const int send_flags = flags | MSG_NOSIGNAL;
    *errval = 0;
    
    /* Try sendmsg() first */
    struct msghdr msg = {0, 0, 
                         iov, 
                         iov_len, 
                         0, 0, 0};
    ssize_t sent = ::sendmsg(fd, &msg, send_flags);
    
    if (sent < 0)
    {
        sent = 0;
        *errval = errno;
    }
    else if (sent == 0)
    {
        *errval = EPIPE;
    }
    else if (static_cast<size_t>(sent) < tot_len)
    {
        *errval = EAGAIN;
    }
    
    assert(*errval != 0 || tot_len == static_cast<size_t>(sent));
    return sent;
}

/*
 * Push contents of iov starting from offset into byte buffer
 */
static void iov_push(struct iovec iov[], 
                     const size_t iov_len, 
                     const size_t offset, 
                     gu::Buffer& bb)
{
    size_t begin_off = 0;
    size_t end_off = 0;
    for (size_t i = 0; i < iov_len; ++i)
    {
        end_off += iov[i].iov_len;
        if (end_off > offset)
        {
            size_t iov_base_off;
            if (begin_off <= offset)
            {
                /* The first iov_base to read */
                iov_base_off = offset - begin_off;
            }
            else
            {
                iov_base_off = 0;
            }
            
            bb.insert(bb.end(), 
                      static_cast<const byte_t*>(iov[i].iov_base) 
                      + iov_base_off, 
                      static_cast<const byte_t*>(iov[i].iov_base) 
                      + iov[i].iov_len);
        }
        begin_off = end_off;
    }
}

size_t gu::net::Socket::get_max_pending_len() const
{
    return max_pending;
}


int gu::net::Socket::send_pending(const int send_flags)
{
    assert(pending.size() != 0);
    struct iovec iov[1] = {
        { &pending[0], pending.size() }
    };
    int ret = 0;
    size_t sent = iov_send(fd, 
                           iov, 
                           1,
                           pending.size(),
                           send_flags, 
                           &ret);
    
    pending.erase(pending.begin(), pending.begin() + sent);
    return ret;
}

int gu::net::Socket::send(const Datagram* const dgram, const int flags)
{
    if (get_state() != S_CONNECTED)
    {
        return ENOTCONN;
    }
    
    bool no_block = (get_opt() & O_NON_BLOCKING) ||
        (flags & MSG_DONTWAIT);
    const int send_flags = flags | (no_block == true ? MSG_DONTWAIT : 0) |
        MSG_NOSIGNAL;
    int ret = 0;
    
    // Dgram with offset can't be handled yet
    assert(dgram == 0 || dgram->get_offset() == 0);

    
    if (dgram != 0)
    {
        if (dgram->get_len() == 0)
        {
            gu_throw_error(EINVAL) << "trying to send zero sized dgram to " 
                                   << fd;
        }
        else if (dgram->get_len() > get_mtu())
        {
            gu_throw_error(EMSGSIZE) << "message too long: " 
                                     << dgram->get_len()
                                     << " max allowed: " 
                                     << get_mtu();
        }
    }
    
    if (dgram == 0)
    {
        if (pending.size() > 0)
        {
            ret = send_pending(send_flags);
        }        
        assert(ret != 0 || pending.size() == 0);
        
        switch (ret)
        {
        case 0:
        case EAGAIN:
            break;
        default:
            set_state(S_FAILED, ret);
        }
    } 
    else if (no_block == true &&
             pending.size() + 
             dgram->get_len() + hdrlen > get_max_pending_len())
    {
        // Return directly from here, this is the only case when 
        // return value must not be altered at the end of this method.
        return EAGAIN;
    }
    else if (no_block == true && pending.size() > 0)
    {
        uint32_t len;
        serialize(static_cast<uint32_t>(dgram->get_len()), 
                  reinterpret_cast<byte_t*>(&len), 
                  sizeof(len), 0);
        pending.reserve(pending.size() + hdrlen + dgram->get_len());
        pending.insert(pending.end(), 
                       reinterpret_cast<byte_t*>(&len), 
                       reinterpret_cast<byte_t*>(&len) + sizeof(len));
        pending.insert(pending.end(),
                       dgram->get_header().begin(),
                       dgram->get_header().end());
        pending.insert(pending.end(),
                       dgram->get_payload().begin(),
                       dgram->get_payload().end());
        ret = EAGAIN;
    }
    else
    {
        /* Send any pending bytes first */
        if (pending.size() > 0)
        {
            assert(no_block == false);
            /* Note: mask out MSG_DONTWAIT from flags */
            ret = send_pending(send_flags & ~MSG_DONTWAIT);
        }
        assert(ret == 0 && pending.size() == 0);
        
        uint32_t hdr(0);
        serialize(static_cast<uint32_t>(dgram->get_len()), 
                  reinterpret_cast<byte_t*>(&hdr), sizeof(hdr), 0);
        
        struct iovec iov[3] = {
            {&hdr, sizeof(hdr)},
            {const_cast<byte_t*>(&dgram->get_header()[0]), 
             dgram->get_header().size()},
            {const_cast<byte_t*>(&dgram->get_payload()[0]), 
             dgram->get_payload().size()}
        };

        size_t sent = iov_send(fd, iov, 3, 
                               sizeof(hdr) + dgram->get_len(), 
                               send_flags, &ret);
        switch (ret)
        {
        case EAGAIN:
            assert(no_block == true);
            iov_push(iov, 3, sent, pending);
            break;
        case 0:
            /* Everything went fine */
            assert(sent == dgram->get_len() + hdrlen);
            break;
        default:
            /* Error occurred */
            set_state(S_FAILED, ret);
        }
    }
    
    if (ret == EAGAIN)
    {
        if ((get_event_mask() & E_OUT) == 0)
        {
            net.set_event_mask(this, get_event_mask() | E_OUT);
        }
        ret = 0;
    }
    else if (pending.size() == 0 && (get_event_mask() & E_OUT) != 0)
    {
        net.set_event_mask(this, get_event_mask() & ~E_OUT);
    }
    
    return ret;
}


const gu::net::Datagram* gu::net::Socket::recv(const int flags)
{
    const int recv_flags = (flags & ~MSG_PEEK) |
        ((get_opt() & O_NON_BLOCKING) != 0 ? MSG_DONTWAIT : 0) |
        MSG_NOSIGNAL;
    const bool peek = flags & MSG_PEEK;
    
    if (get_state() != S_CONNECTED)
    {
        err_no = ENOTCONN;
        return 0;
    }
    
    if (recv_buf_offset > hdrlen)
    {
        uint32_t len = 0;
        unserialize(&recv_buf[0], recv_buf_offset, 0, &len);
        
        if (len == 0 || len > max_packet_size)
        {
            log_error << "invalid packet size " << len;
            set_state(S_FAILED, EMSGSIZE);
            return 0;
        }
        
        if (recv_buf_offset >= len + hdrlen)
        {
            dgram = Datagram(Buffer(&recv_buf[0] + hdrlen,
                                    &recv_buf[0] + hdrlen + len));
            if (peek == false)
            {
                memmove(&recv_buf[0], &recv_buf[0] + hdrlen + len,
                        recv_buf_offset - (hdrlen + len));
                recv_buf_offset -= (hdrlen + len);
            }
            return &dgram;
        }
    }
    
    assert(recv_buf_offset < recv_buf.size());
    
    ssize_t recvd = ::recv(fd,
                           &recv_buf[0] + recv_buf_offset, 
                           recv_buf.size() - recv_buf_offset, 
                           recv_flags);
    if (recvd < 0)
    {
        switch (errno)
        {
        case EAGAIN:
            return 0;
        default:
            set_state(S_FAILED, errno);
            return 0;
        }
    }
    else if (recvd == 0)
    {
        set_state(S_CLOSED);
        return 0;
    }
    else
    {
        recv_buf_offset += recvd;
        if (recv_buf_offset >= hdrlen)
        {
            uint32_t len = 0;
            unserialize(&recv_buf[0], recv_buf_offset, 0, &len);
            
            if (len == 0 || len > max_packet_size)
            {
                log_error << "invalid packet size " << len;
                set_state(S_FAILED, EMSGSIZE);
                return 0;
            }
            
            if (len + hdrlen + recv_buf_offset > recv_buf.size())
            {
                recv_buf.resize(len + hdrlen + recv_buf_offset);
            }
            
            if (recv_buf_offset >= len + hdrlen)
            {
                dgram = Datagram(Buffer(&recv_buf[0] + hdrlen, 
                                        &recv_buf[0] + hdrlen + len));
                if (peek == false)
                {
                    memmove(&recv_buf[0], &recv_buf[0] + hdrlen + len,
                            recv_buf_offset - (hdrlen + len));
                    recv_buf_offset -= (hdrlen + len);
                }
                
                return &dgram;
            }
        }
    }
    
    // We should not get here if peek is not set
    assert(peek == true);
    return 0;
}


void gu::net::Socket::release()
{
    net.release(this);
}


class gu::net::SocketList
{
    map<int, Socket*> sockets;
public:
    typedef map<int, Socket*>::iterator iterator;

    SocketList() : sockets() {}

    bool insert(const std::pair<const int, Socket*>& p)
    {
        return sockets.insert(p).second;
    }

    void erase(const int fd)
    {
        sockets.erase(fd);
    }

    Socket* find(const int fd)
    {
        std::map<const int, Socket*>::const_iterator i = sockets.find(fd);

        if (i == sockets.end())
        {
            return 0;
        }

        return i->second;
    }

    iterator begin()
    {
        return sockets.begin();
    }

    iterator end()
    {
        return sockets.end();
    }
};


/**
 * Network event 
 **/

gu::net::NetworkEvent::NetworkEvent(const int event_mask_, Socket* socket_) :
    event_mask (event_mask_),
    socket     (socket_)
{}

int gu::net::NetworkEvent::get_event_mask() const
{
    return event_mask;
}

gu::net::Socket* gu::net::NetworkEvent::get_socket() const
{
    return socket;
}


gu::net::Network::Network() :
    sockets  (new SocketList()),
    released (),
    poll     (Poll::create())
{
    if (pipe(wake_fd) == -1) gu_throw_error(errno) << "could not create pipe";

    poll->insert(PollEvent(wake_fd[0], E_IN, 0));
}


gu::net::Network::~Network()
{
    poll->erase(PollEvent(wake_fd[0], 0, 0));
    closefd(wake_fd[1]);
    closefd(wake_fd[0]);
    
    /* NOTE: Deleting socket modifies also socket list (erases corresponding
     * entry) and iterator becomes invalid. Therefore i = sockets->begin()
     * on each iteration. */
    for (SocketList::iterator i = sockets->begin(); 
         i != sockets->end(); i = sockets->begin())
    {
        delete i->second;
    }

    delete sockets;
    delete poll;
}


gu::net::Socket* gu::net::Network::connect(const string& addr)
{
    Socket* sock = new Socket(*this);
    sock->connect(addr);
    insert(sock);

    if (sock->get_state() == Socket::S_CONNECTED)
    {
        set_event_mask(sock, E_IN);
    }
    else if (sock->get_state() == Socket::S_CONNECTING)
    {
        set_event_mask(sock, E_OUT);
    }

    return sock;
}


gu::net::Socket* gu::net::Network::listen(const string& addr)
{
    Socket* sock = new Socket(*this);
    sock->listen(addr);
    insert(sock);
    set_event_mask(sock, E_IN);
    return sock;
}


void gu::net::Network::insert(Socket* sock)
{
    if (sockets->insert(make_pair(sock->get_fd(), sock)) == false)
    {
        delete sock;
        gu_throw_fatal << "Failed to add socket to sockets map";
    }

    poll->insert(PollEvent(sock->get_fd(), sock->get_event_mask(), sock));
}


void gu::net::Network::erase(Socket* sock)
{
    /* Erases socket from poll set */
    poll->erase(PollEvent(sock->get_fd(), 0, sock));
    sockets->erase(sock->get_fd());
}


void gu::net::Network::release(Socket* sock)
{
    assert(sock->get_state() == Socket::S_CLOSED);
    released.push_back(sock);
}


gu::net::Socket* gu::net::Network::find(int fd)
{
    return sockets->find(fd);
}


void gu::net::Network::set_event_mask(Socket* sock, const int mask)
{
    if (find(sock->get_fd()) == 0)
    {
        gu_throw_fatal << "Socket " << sock->get_fd()
                       << " not found from socket set";
    }
    
    poll->modify(PollEvent(sock->get_fd(), mask, sock));
    sock->set_event_mask(mask);
}


gu::net::NetworkEvent gu::net::Network::wait_event(const Period& timeout,
                                                   const bool auto_handle)
{
    Socket* sock = 0;
    int revent = 0;
    
    for_each(released.begin(), released.end(), DeleteObject());
    released.clear();
    
    // First return sockets that have pending unread data
    for (SocketList::iterator i = sockets->begin(); i != sockets->end();
         ++i)
    {
        Socket* s = i->second;
        if (s->get_state()       == Socket::S_CONNECTED     && 
            (s->get_event_mask() & E_IN) != 0 &&
            s->has_unread_data() == true                    &&
            s->recv(MSG_PEEK)    != 0)
        {
            return NetworkEvent(E_IN, s);
        }
        // Some validation
        assert(s->pending.size() == 0 || 
               (s->get_event_mask() & E_OUT) != 0);
    }
    
    if (poll->empty() == true)
    {
        poll->poll(timeout);
    }
    
    do
    {
        if (poll->empty())
        {
            return NetworkEvent(E_EMPTY, 0);
        }
        
        PollEvent ev = poll->front();
        poll->pop_front();
        
        // This is a wake-up socket
        if (ev.get_user_data() == 0)
        {
            byte_t buf[1];
            
            if (read(wake_fd[0], buf, sizeof(buf)) != 1)
            {
                gu_throw_error(errno) << "Could not read pipe";
            }
            
            return NetworkEvent(E_EMPTY, 0);
        }
        
        sock = static_cast<Socket*>(ev.get_user_data());
        revent = ev.get_events();
        
        switch (sock->get_state())
        {
        case Socket::S_CLOSED:
            log_warn << "closed socket " << sock->get_fd() << " in poll set";
            // gu_throw_fatal << "closed socket in poll set");
            break;
        case Socket::S_CONNECTING:
            if (revent & E_OUT)
            {
                // Should do more deep inspection here?
                sock->set_state(Socket::S_CONNECTED);
                set_event_mask(sock, E_IN);
                revent = E_CONNECTED;
            }
            break;
        case Socket::S_LISTENING:
            if ((revent & E_IN) && auto_handle == true)
            {
                Socket* acc = sock->accept();
                revent = E_ACCEPTED;
                sock = acc;
            }
            break;
        case Socket::S_FAILED:
            log_warn << "failed socket " << sock->get_fd() << " in poll set";
            // gu_throw_fatal << "failed socket in poll set";
            break;
        case Socket::S_CONNECTED:
            if (revent & E_IN)
            {
                const gu::net::Datagram* dm = sock->recv(MSG_PEEK);
                if (dm == 0)
                {
                    switch (sock->get_state())
                    {
                    case gu::net::Socket::S_FAILED:
                        revent = E_ERROR;
                        break;
                    case gu::net::Socket::S_CLOSED:
                        revent = E_CLOSED;
                        break;
                    case gu::net::Socket::S_CONNECTED:
                        sock = 0;
                    default:
                        break;
                    }
                }
            }
            else if (revent & E_OUT)
            {
                if (auto_handle == true)
                {
                    sock->send();
                    sock = 0;
                }
            }
            break;
        case Socket::S_MAX:
            gu_throw_fatal << "Invalid socket state: " << Socket::S_MAX;
            break;
        }
    }
    while (sock == 0);
    
    return NetworkEvent(revent, sock);
}


void gu::net::Network::interrupt()
{
    byte_t buf[1] = {'i'};
    if (write(wake_fd[1], buf, sizeof(buf)) != 1)
    {
        int err = errno;
        log_error << "unable to write to pipe: " << strerror(err);
        gu_throw_error(err) << "unable to write to pipe";
    }
}

