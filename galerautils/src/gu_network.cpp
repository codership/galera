/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/* Galerautil C++ includes */
#include "gu_network.hpp"
#include "gu_resolver.hpp"
#include "gu_url.hpp"
#include "gu_logger.hpp"
#include "gu_epoll.hpp"

/* Galerautil C includes */

#include "gu_byteswap.h"

/* C-includes */
#include <cstring>
#include <cerrno>
#include <cassert>

/* STD/STL includes */
#include <stdexcept>
#include <sstream>
#include <map>
#include <vector>

/* System includes */
#include <netdb.h>
#include <fcntl.h>

/* Using stuff to improve readability */
using std::string;
using std::istringstream;
using std::vector;
using std::map;
using std::make_pair;

class OptionMap
{
    map<const string, int> opt_map;
public:
    
    void insert(const string& key, const int val)
    {
        if (opt_map.insert(make_pair(key, val)).second == false)
        {
            throw std::logic_error("duplicate key");
        }
    }
    
    OptionMap() :
        opt_map()
    {
        insert("socket.non_blocking", gu::net::Socket::O_NON_BLOCKING);
    }
    
    std::map<const string, int>::const_iterator find(const string& key)
    {
        std::map<const string, int>::const_iterator i;
        if ((i = opt_map.find(key)) == opt_map.end())
        {
            log_error << "invalid key '" << key << "'";
            throw std::logic_error("invalid key");
        }
        return i;
    }
};

static OptionMap option_map;

template <class T> class Option
{
    int opt;
    T val;
public:
    Option(int opt_, T val_) :
        opt(opt_),
        val(val_)
    {
    }
    int get_opt() const
    {
        return opt;
    }

    const T& get_val() const
    {
        return val;
    }
};

template <class T> Option<T> get_option(const string& key, const string& val)
{
    
    map<const string, int>::const_iterator i = option_map.find(key);
    istringstream is(val);
    T ret;
    is >> ret;
    if (is.fail())
    {
        throw std::invalid_argument("");
    }
    return Option<T>(i->second, ret);
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




/**
 *
 */

class gu::net::ByteBuffer
{
    byte_t* buf;
    size_t buf_len;
    size_t buf_size;

    ByteBuffer(const ByteBuffer&);
    void operator=(const ByteBuffer&);
public:
    ByteBuffer(const size_t init_size = 0) :
        buf(0),
        buf_len(0),
        buf_size(0)
    {
        resize(init_size);
    }

    ~ByteBuffer()
    {
        free(buf);
    }
    
    void resize(const size_t to_size)
    {
        void* tmp = realloc(buf, to_size);
        if (to_size > 0 && tmp == 0)
        {
            log_fatal << "failed to allocate " << to_size;
            throw std::bad_alloc();
        }
        buf = reinterpret_cast<byte_t*>(tmp);
        buf_size = to_size;
    }
    
    void push(const byte_t* b, const size_t blen)
    {
        if (buf_len + blen > buf_size)
        {
            resize(buf_len + blen);
        }
        assert(buf_len + blen <= buf_size);
        memcpy(buf + buf_len, b, blen);
        buf_len += blen;
    }

    void pop(const size_t blen)
    {
        assert(blen <= buf_len);
        memmove(buf, buf + blen, buf_len - blen);
        buf_len -= blen;
    }
    
    const byte_t* get_buf() const
    {
        return buf;
    }
    size_t get_len() const
    {
        return buf_len;
    }
    size_t get_size() const
    {
        return buf_size;
    }
    
    
    void reserve(const size_t blen)
    {
        assert(buf_len + blen <= buf_size);
        buf_len += blen;
    }

    byte_t* get_buf(size_t offset = 0)
    {
        return buf + offset;
    }

    size_t get_available(size_t offset = 0)
    {
        return buf_size - offset;
    }
};

/**************************************************************************
 * Datagram implementation
 **************************************************************************/


gu::net::Datagram::Datagram(const byte_t* buf_, const size_t buflen_) :
    const_buf(buf_),
    buf(0),
    buflen(buflen_)
{
}

gu::net::Datagram::Datagram(const Datagram& dgram) :
    const_buf(0),
    buf(0),
    buflen(dgram.get_buflen())
{
    if (buflen > 0)
    {
        buf = new byte_t[buflen];
    }
    memcpy(buf, dgram.get_buf(), buflen);
}

gu::net::Datagram::~Datagram()
{
    delete[] buf;
}

const gu::net::byte_t* gu::net::Datagram::get_buf(const size_t offset) const
{
    if (offset > buflen)
    {
        throw std::out_of_range("datagram offset out of range");
    }
    const byte_t* b = const_buf ? const_buf : buf;
    return b + offset;
}

size_t gu::net::Datagram::get_buflen(const size_t offset) const
{
    if (offset > buflen)
    {
        throw std::out_of_range("datagram offset out of range");
    }
    return buflen - offset;
}

void gu::net::Datagram::reset(const byte_t* b, const size_t blen)
{
    if (buf != 0)
    {
        throw std::logic_error("invalid state");
    }
    const_buf = b;
    buflen = blen;
}


/**************************************************************************
 * Socket implementation
 **************************************************************************/

/* 
 * State handling
 */
void gu::net::Socket::set_state(const State s, const int err)
{
    /* Allowed state transitions */
    static const bool allowed[S_MAX][S_MAX] = {
        /* CL, CTING, CTED, LIS, FAIL */
        {false, true, true, true, true},   /* CL     */
        {true, false, true, false, true},  /* CTING  */
        {true, false, false, false, true}, /* CTED   */
        {true, false, false, false, true}, /* LIS    */
        {true, false, false, false, false} /* FAIL   */
    };
    if (allowed[get_state()][s] == false)
    {
        log_error << "invalid state change " << state << " -> " << s;
        throw std::logic_error("invalid state change");
    }
    state = s;
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


/*
 * Ctor/dtor
 */

gu::net::Socket::Socket(Network& net_, 
                   const int fd_,
                   const int options_,
                   const sockaddr* local_sa_,
                   const sockaddr* remote_sa_,
                   const size_t sa_size_) :
    fd(fd_),
    err_no(0),
    options(options_),
    event_mask(0),
    local_sa(),
    remote_sa(),
    sa_size(sa_size_),
    dgram_offset(0),
    complete(false),
    dgram(),
    recv_buf(new ByteBuffer(1 << 16)),
    pending(new ByteBuffer(1 << 10)),
    state(S_CLOSED),
    net(net_)
{
    memset(&local_sa, 0, sizeof(local_sa));
    memset(&remote_sa, 0, sizeof(remote_sa));
    if (local_sa_ != 0)
    {
        local_sa = *local_sa_;
    }
    if (remote_sa_ != 0)
    {
        remote_sa = *remote_sa_;
    }
}

gu::net::Socket::~Socket()
{
    if (fd != -1)
    {
        if (get_state() == S_CLOSED)
        {
            throw std::logic_error("fd != -1 but state S_CLOSED");
        }
        close();
    }
    delete recv_buf;
    delete pending;
}

/*
 * Construct URL from string. If no scheme-authority separator is not 
 * found from string, string is prefixed with tcp scheme.
 */
static gu::URL get_url(const string& str)
{
    string real_str;
    if (str.find("://") == string::npos)
    {
        real_str = "tcp://" + str;
    }
    else
    {
        real_str = str;
    }
    return gu::URL(real_str);
}

/*
 * Fill in addrinfo according to addr URL
 */
static void get_addrinfo(const gu::URL& url, struct addrinfo** ai)
{
    string scheme = url.get_scheme();
    string addr = url.get_authority();
    gu::net::Resolver::resolve(scheme, addr, ai);
}

void gu::net::Socket::open_socket(const string& addr)
{
    struct addrinfo* ai(0);    
    URL url(get_url(addr));
    get_addrinfo(url, &ai);
    
    if (ai == 0 || ai->ai_addr == 0)
    {
        throw std::runtime_error("could not resolve address");
    }
    
    int ai_family = ai->ai_family;
    int ai_socktype = ai->ai_socktype;
    int ai_protocol = ai->ai_protocol;
    /* TODO: For now on assume first entry is always the correct one 
     * until some heuristics to guess best possible canditate 
     * is implemented */
    local_sa = *ai->ai_addr;
    sa_size = ai->ai_addrlen;
    
    freeaddrinfo(ai);


    const URLQueryList& ql = url.get_query_list();
    for (URLQueryList::const_iterator i = ql.begin(); i != ql.end(); ++i)
    {
        Option<int> opt = get_option<int>(i->first, i->second);
        switch (opt.get_opt())
        {
        case O_NON_BLOCKING:
            if (opt.get_val())
            {
                options |= O_NON_BLOCKING;
            }
            else
            {
                options &= ~O_NON_BLOCKING;
            }
            break;
        default:
            throw std::logic_error("invalid option");
        }
    }
    
    if ((fd = ::socket(ai_family, ai_socktype, ai_protocol)) == -1)
    {
        throw std::runtime_error("could not create socket");
    }
    
    if ((getopt() & O_NON_BLOCKING))
    {
        if (::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
        {
            int err = errno;
            log_error << "could not set fd non blocking " << strerror(err);
        }
    }

}

void gu::net::Socket::connect(const string& addr)
{
    open_socket(addr);
    
    if (::connect(fd, &local_sa, sa_size) == -1)
    {
        if ((options & O_NON_BLOCKING) && errno == EINPROGRESS)
        {
            log_debug << "non blocking connecting";
            set_state(S_CONNECTING);
        }
        else
        {
            set_state(S_FAILED, errno);
            throw std::runtime_error("connect failed");
        }
    }
    else
    {
        set_state(S_CONNECTED);
    }
    assert(get_state() == S_CONNECTING || get_state() == S_CONNECTED);
}

void gu::net::Socket::close()
{
    if (fd == -1 || get_state() == S_CLOSED)
    {
        log_error << "socket not open: " << fd << " " << get_state();
        throw std::logic_error("socket not open");
    }
    
    /* Close socket first to interrupt possible senders */
    log_debug << "closing socket " << fd;
    int err = closefd(fd);
    if (err != 0)
    {
        set_state(S_FAILED, err);
        throw std::runtime_error("close failed");
    }
    
    /* Reset fd only after calling net.erase() */
    net.erase(this);
    
    fd = -1;
    set_state(S_CLOSED);
}

void gu::net::Socket::listen(const std::string& addr, const int backlog)
{
    open_socket(addr);

    const int reuse = 1;
    const socklen_t reuselen = sizeof(reuse);
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, reuselen) == -1)
    {
        set_state(S_FAILED, errno);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }
    
    if (::bind(fd, &local_sa, sa_size) == -1)
    {
        set_state(S_FAILED, errno);
        throw std::runtime_error("bind failed");
    }
    
    if (::listen(fd, backlog) == -1)
    {
        set_state(S_FAILED, errno);
        throw std::runtime_error("listen failed");
    }
    
    set_state(S_LISTENING);
}

gu::net::Socket* gu::net::Socket::accept()
{
    
    int acc_fd;

    sockaddr acc_sa;
    socklen_t sasz = sizeof(acc_sa);
    memset(&acc_sa, 0, sizeof(acc_sa));
    if ((acc_fd = ::accept(fd, &acc_sa, &sasz)) == -1)
    {
        set_state(S_FAILED, errno);
        throw std::runtime_error("accept failed");
    }
    
    if (sasz != sa_size)
    {
        set_state(S_FAILED, errno);
        throw std::runtime_error("sockaddr size mismatch");
    }
    
    /* TODO: Set options before returning */
    
    Socket* ret = new Socket(net, acc_fd, options, &local_sa, &acc_sa, sasz);
    ret->set_state(S_CONNECTED);
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
        assert(*errval == 0);
        // sent = slow_send(fd, iov, iov_len, sent, flags, errval);
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
                     gu::net::ByteBuffer* bb)
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
            bb->push((gu::net::byte_t*)iov[i].iov_base + iov_base_off, 
                     iov[i].iov_len - iov_base_off);
        }
        begin_off = end_off;
    }
}

size_t gu::net::Socket::get_max_pending_len() const
{
    return 1 << 20;
}



static void write_hdr(gu::net::byte_t* buf, const size_t buflen, uint32_t len)
{
    assert(buf != 0 && buflen == 4);
    *(uint32_t*)buf = htogl(len);
}

static void read_hdr(const gu::net::byte_t* buf, 
                     const size_t buflen, 
                     uint32_t *len)
{
    assert(buf != 0 && buflen >=  4 && len != 0);
    *len = gtohl(*(uint32_t*)buf);
}

int gu::net::Socket::send_pending(const int send_flags)
{
    struct iovec iov[1] = {
        {const_cast<byte_t*>(pending->get_buf()), pending->get_len()}
    };
    int ret = 0;
    size_t sent = iov_send(fd, 
                           iov, 
                           1,
                           pending->get_len(),
                           send_flags, 
                           &ret);
    pending->pop(sent);
    return ret;
}

int gu::net::Socket::send(const Datagram* const dgram, const int flags)
{
    if (get_state() != S_CONNECTED)
    {
        return ENOTCONN;
    }
    bool no_block = (getopt() & O_NON_BLOCKING) ||
        (flags & MSG_DONTWAIT);
    const int send_flags = flags | (no_block ? MSG_DONTWAIT : 0);
    int ret = 0;

    byte_t hdr[4];
    const size_t hdrlen = sizeof(hdr);
    
    if (dgram == 0)
    {
        if (pending->get_len() > 0)
        {
            ret = send_pending(send_flags);
        }
        assert(ret != 0 || pending->get_len() == 0);
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
             pending->get_len() + dgram->get_buflen() + hdrlen > 
             get_max_pending_len())
    {
        ret = EAGAIN;
    }
    else if (no_block == true && pending->get_len() > 0)
    {
        write_hdr(hdr, hdrlen, dgram->get_buflen());
        pending->push(hdr, hdrlen);
        pending->push(dgram->get_buf(), dgram->get_buflen());
    }
    else
    {
        /* Send any pending bytes first */
        if (pending->get_len() > 0)
        {
            assert(no_block == false);
            /* Note: mask out MSG_DONTWAIT from flags */
            ret = send_pending(send_flags & ~MSG_DONTWAIT);
        }
        assert(pending->get_len() == 0);
        assert(ret == 0);
        write_hdr(hdr, hdrlen, dgram->get_buflen());
        struct iovec iov[2] = {
            {hdr, hdrlen},
            {const_cast<byte_t*>(dgram->get_buf()), dgram->get_buflen()}
        };
        size_t sent = iov_send(fd, iov, 2, hdrlen + dgram->get_buflen(), 
                               send_flags, &ret);
        switch (ret)
        {
        case EAGAIN:
            assert(no_block == true);
            iov_push(iov, 2, sent, pending);
            break;
        case 0:
            /* Everything went fine */
            assert(sent == dgram->get_buflen() + hdrlen);
            break;
        default:
            /* Error occurred */
            set_state(S_FAILED, ret);
        }
    }
    // log_debug << "return: " << ret;
    if (ret == EAGAIN and not get_event_mask() & gu::net::NetworkEvent::E_OUT)
    {
        net.set_event_mask(this, get_event_mask() & gu::net::NetworkEvent::E_OUT);
    }
    return ret;
}


const gu::net::Datagram* gu::net::Socket::recv(const int flags)
{
    const size_t hdrlen = sizeof(uint32_t);
    const int recv_flags = (flags & ~MSG_PEEK) |
        (getopt() & O_NON_BLOCKING ? MSG_DONTWAIT : 0);
    const bool peek = flags & MSG_PEEK;
    
    if (get_state() != S_CONNECTED)
    {
        err_no = ENOTCONN;
        return 0;
    }
    
    if (complete == true)
    {
        if (peek == false)
        {
            dgram_offset = hdrlen + dgram.get_len();
            complete = false;
        }
        return &dgram;
    }
    
    if (dgram_offset > 0)
    {
        recv_buf->pop(dgram_offset);
        dgram_offset = 0;
    }
    

    
    ssize_t recvd = ::recv(fd, 
                           recv_buf->get_buf(recv_buf->get_len()), 
                           recv_buf->get_available(recv_buf->get_len()), 
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
            //throw std::runtime_error("recv failed");
        }
    }
    else if (recvd == 0)
    {
        close();
        return 0;
        // throw std::runtime_error("recv failed");
    }
    else
    {
        recv_buf->reserve(recvd);
        if (recv_buf->get_len() > hdrlen)
        {
            uint32_t len = 0;
            read_hdr(recv_buf->get_buf(), hdrlen, &len);
            if (len + hdrlen > recv_buf->get_size())
            {
                recv_buf->resize(len + hdrlen);
            }
            else if (recv_buf->get_len() >= len + hdrlen)
            {
                dgram.reset(recv_buf->get_buf(hdrlen), len);
                if (peek == false)
                {
                    dgram_offset = len + hdrlen;
                    complete = false;
                }
                else
                {
                    complete = true;
                }
                return &dgram;
            }
        }
    }
    return 0;
}

int gu::net::Socket::getopt() const
{
    return options;
}

void gu::net::Socket::setopt(const int opts)
{
    options = opts;
}


class gu::net::SocketList
{
    map<const int, Socket*> sockets;
public:
    typedef map<const int, Socket*>::iterator iterator;

    SocketList() : 
        sockets()
    {

    }

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
    event_mask(event_mask_),
    socket(socket_)
{

}

int gu::net::NetworkEvent::get_event_mask() const
{
    return event_mask;
}

gu::net::Socket* gu::net::NetworkEvent::get_socket() const
{
    return socket;
}

/**
 *
 **/

gu::net::Network::Network() :
    sockets(new SocketList()),
    poll(new EPoll())
{
    if (pipe(wake_fd) == -1)
    {
        throw std::runtime_error("could not create pipe");
    }
    poll->insert(EPollEvent(wake_fd[0], NetworkEvent::E_IN, 0));
}

gu::net::Network::~Network()
{
    poll->erase(EPollEvent(wake_fd[0], 0, 0));
    closefd(wake_fd[1]);
    closefd(wake_fd[0]);
    
    /* NOTE: Deleting socket modifies also socket list (erases corresponding
     * entry) and iterator becomes invalid. Therefore i = sockets->begin()
     * on each iteration. */
    for (SocketList::iterator i = sockets->begin(); 
         i != sockets->end(); i = sockets->begin())
    {
        log_warn << "open socket in network dtor: " << i->first;
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
        set_event_mask(sock, NetworkEvent::E_IN);
    }
    else if (sock->get_state() == Socket::S_CONNECTING)
    {
        set_event_mask(sock, NetworkEvent::E_OUT);
    }
    return sock;
}

gu::net::Socket* gu::net::Network::listen(const string& addr)
{
    Socket* sock = new Socket(*this);
    sock->listen(addr);
    insert(sock);
    set_event_mask(sock, NetworkEvent::E_IN);
    return sock;
}

void gu::net::Network::insert(Socket* sock)
{
    if (sockets->insert(make_pair(sock->get_fd(), sock)) == false)
    {
        delete sock;
        throw std::logic_error("");
    }
    poll->insert(EPollEvent(sock->get_fd(), sock->get_event_mask(), sock));
}

void gu::net::Network::erase(Socket* sock)
{
    /* Erases socket from poll set */
    poll->erase(EPollEvent(sock->get_fd(), 0, sock));
    sockets->erase(sock->get_fd());
}

gu::net::Socket* gu::net::Network::find(int fd)
{
    return sockets->find(fd);
}

void gu::net::Network::set_event_mask(Socket* sock, const int mask)
{
    if (find(sock->get_fd()) == 0)
    {
        log_error << "socket " << sock->get_fd() << " not found from socket set";
        throw std::logic_error("invalid socket");
    }
    poll->modify(EPollEvent(sock->get_fd(), mask, sock));
    sock->set_event_mask(mask);
}

gu::net::NetworkEvent gu::net::Network::wait_event(const long timeout)
{
    Socket* sock = 0;
    int revent = 0;
    
    if (poll->empty())
    {
        poll->poll(timeout);
    }
    
    do
    {
        if (poll->empty())
        {
            return NetworkEvent(NetworkEvent::E_EMPTY, 0);
        }
        
        EPollEvent ev = poll->front();
        poll->pop_front();
        
        if (ev.get_user_data() == 0)
        {
            byte_t buf[1];
            if (read(wake_fd[0], buf, sizeof(buf)) != 1)
            {
                int err = errno;
                log_error << "Could not read pipe: " << strerror(err);
                throw std::runtime_error("");
            }
            return NetworkEvent(NetworkEvent::E_EMPTY, 0);
        }
        
        sock = reinterpret_cast<Socket*>(ev.get_user_data());
        revent = ev.get_events();
        
        switch (sock->get_state())
        {
        case Socket::S_CLOSED:
            log_error << "closed socket " << sock->get_fd() << " in poll set";
            throw std::logic_error("closed socket in poll set");
            break;
        case Socket::S_CONNECTING:
            if (revent & NetworkEvent::E_OUT)
            {
                // Should do more deep inspection here?
                sock->set_state(Socket::S_CONNECTED);
                set_event_mask(sock, NetworkEvent::E_IN);
                revent = NetworkEvent::E_CONNECTED;
            }
            break;
        case Socket::S_LISTENING:
            if (revent & NetworkEvent::E_IN)
            {
                Socket* acc = sock->accept();
                insert(acc);
                set_event_mask(acc, NetworkEvent::E_IN);
                revent = NetworkEvent::E_ACCEPTED;
                sock = acc;
            }
            break;
        case Socket::S_FAILED:
            log_error << "failed socket " << sock->get_fd() << " in poll set";
            throw std::logic_error("failed socket in poll set");
            break;
        case Socket::S_CONNECTED:
            if (revent & NetworkEvent::E_IN)
            {
                const gu::net::Datagram* dm = sock->recv(MSG_PEEK);
                if (dm == 0)
                {
                    switch (sock->get_state())
                    {
                    case gu::net::Socket::S_FAILED:
                        revent = gu::net::NetworkEvent::E_ERROR;
                        break;
                    case gu::net::Socket::S_CLOSED:
                        revent = gu::net::NetworkEvent::E_CLOSED;
                        break;
                    case gu::net::Socket::S_CONNECTED:
                        sock = 0;
                    default:
                        break;
                    }
                }
            }
            else if (revent & NetworkEvent::E_OUT)
            {
                sock->send();
                sock = 0;
            }
            break;
        case Socket::S_MAX:
            throw std::logic_error("");
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
        throw std::runtime_error("unable to write to pipe");
    }
}
