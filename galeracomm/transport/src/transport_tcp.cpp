#include <iostream>

#include "galeracomm/logger.hpp"
#include "transport_tcp.hpp"

#include <fcntl.h>
#include <unistd.h>

#define TCP_CONN_TIMEOUT 3

static bool tcp_addr_to_sa(const char *addr, struct sockaddr *s, size_t *s_size)
{
     struct sockaddr_in *sa;
     char *ipaddr;
     char *port;
     const char *delim;
     
     if (strncmp(addr, "tcp:", strlen("tcp:")) == 0)
	 addr += strlen("tcp:");
     else if (strncmp(addr, "asynctcp:", strlen("asynctcp:")) == 0)
	 addr += strlen("asynctcp:");
     else
	 return false;

     if (!(delim = strchr(addr, ':')))
	 return false;
     
     ipaddr = strndup(addr, delim - addr);
     port = strdup(delim + 1);
     sa = reinterpret_cast<struct sockaddr_in*>(s);
     if (inet_pton(AF_INET, ipaddr, &sa->sin_addr) <= 0) {
	  free(ipaddr);
	  free(port);
	  return false;
     }
     sa->sin_family = AF_INET;
     sa->sin_port = htons(strtol(port, NULL, 0));
     *s_size = sizeof(struct sockaddr_in);
     free(ipaddr);
     free(port);
     return true;
}

static void sa_to_string (const sockaddr* sa, std::string& str)
{
    char buf[256];
    const sockaddr_in* sin = reinterpret_cast<const sockaddr_in*>(sa);
    std::ostringstream os;

    os << inet_ntop (sin->sin_family, &sin->sin_addr, buf, sizeof(buf))
       << ":" << ntohs(sin->sin_port);

    str = os.str();
}

TCPTransport::TCPTransport(const int _fd, const sockaddr& _sa, 
                           const size_t _sa_size, Poll *_poll) :
    fd(_fd),
    no_nagle(1),
    peer(""),
    sa(_sa),
    sa_size(_sa_size),
    poll(_poll),
    max_pending(1024),
    pending_bytes(0),
    recv_buf_size(65536),
    recv_buf (reinterpret_cast<unsigned char*>(::malloc(recv_buf_size))),
    recv_buf_offset(0),
    recv_rb(0),
    up_rb(0),
    pending()
{
    sa_to_string (&sa, peer);
    set_max_pending_bytes(10*1024*1024);
}

class TCPTransportHdr {
    unsigned char raw[sizeof(uint32_t)];
    uint32_t len;
public:
    TCPTransportHdr(const size_t l) : len(l)
    {
	if (write_uint32(len, raw, sizeof(raw), 0) == 0)
	    throw DException("");
    }

    TCPTransportHdr(const unsigned char *buf, const size_t buflen, 
		    const size_t offset) : len(0)
    {
	if (buflen < sizeof(raw) + offset)
	    throw DException("");
	::memcpy(raw, buf + offset, sizeof(raw));
	if (read_uint32(raw, sizeof(raw), 0, &len) == 0)
	    throw DException("");
    }

    const void* get_raw() const {
	return raw;
    }
    const void* get_raw(const size_t off) const {
	return raw + off;
    }
    static size_t get_raw_len() {
	return 4;
    }
    size_t get_len() const {
	return len;
    }
};



void TCPTransport::connect(const char *addr)
{
    if (fd != -1)
	throw DFatalException("TCPTransport::connect(): Already connected or listening");

    if (!tcp_addr_to_sa(addr, &sa, &sa_size))
	throw DFatalException("TCPTransport::connect(): Invalid address");

    if (::strncmp(addr, "tcp:", strlen("tcp:")) == 0)
	set_synchronous();

    if ((fd = ::socket(PF_INET, SOCK_STREAM, 0)) == -1)
	throw DFatalException(::strerror(errno));

    linger lg = {1, TCP_CONN_TIMEOUT};

    if (::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == -1) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle, sizeof(no_nagle)) 
        == -1) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));	
    }

    if (is_synchronous() == false) {
	if (::fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
	    int err = errno;
            closefd(fd);
	    fd = -1;
	    throw DFatalException(::strerror(err));
	}
    }

    if (poll) {
	poll->insert(fd, this);
	poll->set(fd, PollEvent::POLL_IN);
    }

    if (::connect(fd, &sa, sa_size) == -1) {

	if (errno != EINPROGRESS) {
	    throw DFatalException(::strerror(errno));
	}
        else {
	    if (poll)
		poll->set(fd, PollEvent::POLL_OUT);
	    state = TRANSPORT_S_CONNECTING;
	}
    }
    else {
        sa_to_string (&sa, peer);
        set_timeout (TCP_CONN_TIMEOUT);
	state = TRANSPORT_S_CONNECTED;
    }
}

void TCPTransport::close()
{
    if (fd != -1) {
	if (poll)
	    poll->erase(fd);
        closefd(fd);
	fd = -1;
    }
}

void TCPTransport::listen(const char *addr)
{
    LOG_DEBUG(std::string("TCPTransport::listen(") + addr + ")");

    if (fd != -1)
	throw DFatalException("TCPTransport::listen(): Already connected or listening");

    if (!tcp_addr_to_sa(addr, &sa, &sa_size))
	throw DFatalException("TCPTransport::listen(): Invalid address");

    if ((fd = ::socket(PF_INET, SOCK_STREAM, 0)) == -1)
	throw DFatalException("TCPTransport::listen(): Could not open socket");

    if (::strncmp(addr, "tcp:", strlen("tcp:")) == 0)
	set_synchronous();

    if (is_synchronous() == false && ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	throw DFatalException("TCPTransport::listen(): Fcntl failed");

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
	throw DFatalException("TCPTransport::listen(): Setsockopt faild");

    if (::bind(fd, &sa, sa_size) == -1) {
	int err = errno;
	LOG_FATAL(std::string("TCPTransport::listen(): Bind failed to address ") + to_string(&sa) + " failed " +
		     ::strerror(err));
	throw DFatalException("TCPTransport::listen(): Bind failed");
    }

    if (::listen(fd, 128) == -1)
	throw DFatalException("TCPTransport::listen(): Listen failed");

    sa_to_string (&sa, peer);

    if (poll) {
	poll->insert(fd, this);
	poll->set(fd, PollEvent::POLL_IN);
    }

    state = TRANSPORT_S_LISTENING;
}

Transport *TCPTransport::accept(Poll *poll, Protolay *up_ctx)
{


    sockaddr sa;
    socklen_t sa_size = sizeof(sockaddr);
    
    int acc_fd;
    if ((acc_fd = ::accept(fd, &sa, &sa_size)) == -1)
	throw DFatalException("TCPTransport::accept(): Accept failed");
    

    if (is_synchronous() == false && ::fcntl(acc_fd, F_SETFL, O_NONBLOCK)
        == -1) {
        closefd(fd);
	throw DFatalException("TCPTransport::accept(): Fcntl failed");
    }


    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle, sizeof(no_nagle)) 
        == -1) {
	int err = errno;
        closefd(fd);
	throw DFatalException(::strerror(err));	
    }
    
    TCPTransport *ret = new TCPTransport(acc_fd, sa, sa_size, poll);
    if (up_ctx) {
	ret->set_up_context(up_ctx);
    }
    ret->set_timeout (TCP_CONN_TIMEOUT);
    ret->state = TRANSPORT_S_CONNECTED;
    if (poll) {
	poll->insert(ret->fd, ret);
	poll->set(ret->fd, PollEvent::POLL_IN);
    }
    return ret;
}

//
// Send stuff while masking out interrupted system calls. 
// Sends buflen - offset bytes, unless send fails with EAGAIN. 
// In case of EAGAIN, short byte count is returned.
//
//

ssize_t TCPTransport::send_nointr(const void *buf, const size_t buflen, 
				  const size_t offset, int flags)
{
    ssize_t ret;
    ssize_t sent = 0;
    
    if (buflen == offset)
	return 0;
    do {
	do {
	    ret = ::send(fd, static_cast<const char*>(buf) + offset + sent, 
			 buflen - offset - sent, flags);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1 && errno == EAGAIN) {
	    return sent;
	} else if (ret == -1 || ret == 0) {
	    return -1;
	}
	sent += ret;
    } while (size_t(sent) + offset < buflen);
    return sent;
}

class DummyPollContext : public PollContext {
    void handle(const int fd, const PollEnum pe) {
	// 
    }
};

static int tmp_poll(int fd, PollEnum pe, int tout, PollContext *ctx)
{
    DummyPollContext dummy;
    Poll *tmp_poll = Poll::create("Def");
    LOG_DEBUG("tmp_poll");
    tmp_poll->insert(fd, ctx ? ctx : &dummy);
    tmp_poll->set(fd, pe);
    int ret = tmp_poll->poll(tout);
    delete tmp_poll;
    return ret;
}

int TCPTransport::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    if (state != TRANSPORT_S_CONNECTED)
	return ENOTCONN;
    if (is_synchronous())
	return TCPTransport::send(wb, dm);
    
    if (pending_bytes + wb->get_totlen() > max_pending_bytes) {
	/* was: pending.size() == max_pending */
	LOG_DEBUG("TCPTransport::handle_down(): Contention");
	if (contention_tries > 0 ) {
	    for (unsigned long i = 0; 
		 i < contention_tries && 
		     pending_bytes + wb->get_totlen() > 
		     max_pending_bytes; i++) {
		tmp_poll(fd, PollEvent::POLL_OUT, contention_tout, this);
	    }
	}

	if (pending_bytes + wb->get_totlen() > max_pending_bytes) {
	    LOG_DEBUG("TCPTransport::handle_down(): Contention not cleared, return EAGAIN");
	    return EAGAIN;
	}
    }
    
    TCPTransportHdr hdr(wb->get_totlen());
    wb->prepend_hdr(hdr.get_raw(), hdr.get_raw_len());
    
    if (pending.size()) {
	WriteBuf *wb_copy = wb->copy();
	pending.push_back(PendingWriteBuf(wb_copy, 0));
	pending_bytes += wb->get_totlen();
	LOG_DEBUG("TCPTransport::handle_down(): Appended to pending");
	goto out_success;
    } else {
	ssize_t ret;
	size_t sent = 0;
	
	if ((ret = send_nointr(wb->get_hdr(), wb->get_hdrlen(), 
			       0, wb->get_totlen() > wb->get_hdrlen() ? MSG_MORE : 0)) == -1)
	    goto out_epipe;
	
	sent = ret;
	if (sent != wb->get_hdrlen()) {
	    WriteBuf *wb_copy = wb->copy();
	    pending.push_back(PendingWriteBuf(wb_copy, sent));
	    pending_bytes += wb->get_totlen();
	    if (poll)
		poll->set(fd, PollEvent::POLL_OUT);
	    LOG_DEBUG("TCPTransport::handle_down(): Appended to pending in header send");
	    goto out_success;
	}
	
	if ((ret = send_nointr(wb->get_buf(), wb->get_len(), 0, 0)) == -1)
	    goto out_epipe;
	
	
	sent += ret;
	if (sent != wb->get_len() + wb->get_hdrlen()) {
	    WriteBuf *wb_copy = wb->copy();
	    pending.push_back(PendingWriteBuf(wb_copy, sent));
	    pending_bytes += wb->get_totlen();
	    if (poll)
		poll->set(fd, PollEvent::POLL_OUT);
	    LOG_DEBUG("TCPTransport::handle_down(): Appended to pending in payload send");
	}
    }
    
out_success:
    wb->rollback_hdr(hdr.get_raw_len());
    return 0;
    
out_epipe:
    LOG_WARN("TCPTransport::handle_down(): Broken pipe");
    wb->rollback_hdr(hdr.get_raw_len());
    return EPIPE;
}


int TCPTransport::recv_nointr(int flags)
{
    
    if (recv_buf_offset < TCPTransportHdr::get_raw_len()) {
	ssize_t ret;

	do {
	    ret = ::recv(fd,
                         recv_buf + recv_buf_offset, 
			 TCPTransportHdr::get_raw_len() - recv_buf_offset,
			 flags);
	} while (ret == -1 && errno == EINTR);

	if (ret == -1 && errno == EAGAIN) {
	    log_debug << "TCPTransport::recv_nointr(): "
                      << "Return EAGAIN in header recv"; 
	    return EAGAIN;
	}
	else if (ret == -1 || ret == 0) {
            ret = (ret == -1) ? errno : EPIPE;
	    log_debug << "TCPTransport::recv_nointr(): Return "
                      << ret << " (" << strerror(ret) << ") in header recv"; 
	    return ret;
        }

	recv_buf_offset += ret;

	if (recv_buf_offset < TCPTransportHdr::get_raw_len()) {
	    log_debug << "TCPTransport::recv_nointr(): "
                      <<"Return EAGAIN in header recv"; 
	    return EAGAIN;
	}
    }
    
    TCPTransportHdr hdr(recv_buf, recv_buf_offset, 0);
    
    if (recv_buf_size < hdr.get_len() + hdr.get_raw_len()) {
	recv_buf = reinterpret_cast<unsigned char*>(
	    ::realloc(recv_buf, hdr.get_len() + hdr.get_raw_len()));
    }
    
    if (hdr.get_len() == 0) {
	LOG_DEBUG("TCPTransport::recv_nointr(): Zero len message"); 
	return 0;
    }
    
    do {
	ssize_t ret;    
	do {
	    ret = ::recv(fd, recv_buf + recv_buf_offset,
			 hdr.get_len() - (recv_buf_offset - hdr.get_raw_len()),
			 flags);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1 && errno == EAGAIN) {
	    LOG_DEBUG("TCPTransport::recv_nointr(): Return EAGAIN in body recv"); 
	    return EAGAIN;
	}
	else if (ret == -1 || ret == 0) {
	    LOG_DEBUG("TCPTransport::recv_nointr(): Return error in body recv"); 
	    return ret == -1 ? errno : EPIPE;
	}
	recv_buf_offset += ret;
    } while (recv_buf_offset < hdr.get_len() + hdr.get_raw_len());
    return 0;
}

int TCPTransport::recv_nointr()
{
    return recv_nointr(MSG_DONTWAIT);
}

int TCPTransport::handle_pending()
{
    LOG_DEBUG("TCPTransport::handle_pending()");
    if (pending.size() == 0)
	return 0;
    
    std::deque<PendingWriteBuf>::iterator i;
    while (pending.size()) {
	i = pending.begin();
	ssize_t ret = 0;
	if (i->offset < i->wb->get_hdrlen()) {
	    if ((ret = send_nointr(i->wb->get_hdr(), 
				   i->wb->get_hdrlen(), i->offset, 
				   i->wb->get_len() ? MSG_MORE : 0)) == -1)
		return EPIPE;
	    i->offset += ret;
	    if (i->offset != i->wb->get_hdrlen()) {
		LOG_DEBUG("TCPTransport::handle_pending(): Return EAGAIN");
		return EAGAIN;
	    }
	}
	ret = 0;
	if (i->wb->get_len() &&
	    (ret = send_nointr(i->wb->get_buf(), i->wb->get_len(),
			       i->offset - i->wb->get_hdrlen(), 0)) == -1)
	    return EPIPE;
	
	i->offset += ret;
	if (i->offset != i->wb->get_totlen()) {
	    LOG_DEBUG("TCPTransport::handle_pending(): Return EAGAIN");
	    return EAGAIN;
	}
	
	pending_bytes -= i->offset;
	delete i->wb;
	pending.pop_front();
    }
    return 0;
}

void TCPTransport::handle(const int fd, const PollEnum pe)
{
    assert(fd == this->fd);
    if (pe & PollEvent::POLL_OUT) {
	LOG_DEBUG("TCPTransport::handle(): PollEvent::POLL_OUT");
	int ret;
	if (state == TRANSPORT_S_CONNECTING) {
	    int err = 0;
	    socklen_t errlen = sizeof(err);
	    poll->unset(fd, PollEvent::POLL_OUT);
	    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1)
		throw DFatalException("TCPTransport::handle(): Getsockopt failed");
	    if (err == 0) {
		state = TRANSPORT_S_CONNECTED;
	    } else {
		this->error_no = err;
		state = TRANSPORT_S_FAILED;
	    }
	    pass_up(0, 0, 0);
	} else if ((ret = handle_pending()) == 0) {
	    poll->unset(fd, PollEvent::POLL_OUT);
	} else if (ret != EAGAIN) {
	    // Something bad has happened
	    this->error_no = ret;
	    state = TRANSPORT_S_FAILED;
	    pass_up(0, 0, 0);
	    LOG_DEBUG("TCPTransport::handle(): Failed");
	}
    }

    if (pe & PollEvent::POLL_IN) {
	if (state == TRANSPORT_S_CONNECTED) {
	    ssize_t ret = recv_nointr();
	    if (ret == 0) {
		ReadBuf rb(recv_buf, recv_buf_offset, true);
		pass_up(&rb, TCPTransportHdr::get_raw_len(), 0);
		recv_buf_offset = 0;
	    } else if (ret != EAGAIN) {
		this->error_no = ret;
		state = TRANSPORT_S_FAILED;
		pass_up(0, 0, 0);
		LOG_DEBUG("TCPTransport::handle(): Failed");
	    }
	} else if (state == TRANSPORT_S_LISTENING) {
	    pass_up(0, 0, 0);
	}
    }

    if (pe & PollEvent::POLL_HUP) {
	this->error_no = ENOTCONN;
	state = TRANSPORT_S_FAILED;
	pass_up(0, 0, 0);
    }

    if (pe & PollEvent::POLL_INVAL)
	throw
            DFatalException("TCPTransport::handle(): Invalid file descriptor");

    if (pe & PollEvent::POLL_ERR) {
	int err = 0;
	socklen_t errlen = sizeof(err);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
            if (EBADF == errno) {
                log_warn << "TCPTransport::handle(): lost connectivity to "
                         << peer;
            }
            else {
                log_warn << "TCPTransport::handle(): socket error: " << errno
                         << " " << strerror (errno);
            }
        }
        else {
	    log_warn << "TCPTransport::handle(): poll error: " << err << " "
                     << strerror (err);
        }
    }
}





int TCPTransport::send(WriteBuf *wb, const ProtoDownMeta *dm)
{

    TCPTransportHdr hdr(wb->get_totlen());
    wb->prepend_hdr(hdr.get_raw(), hdr.get_raw_len());
    ssize_t ret;
    size_t sent = 0;
    int err = 0;

    while (pending.size() && (err = handle_pending()) == 0) {}
    if (err)
	return err;
    
    while (sent != wb->get_hdrlen()) {
	ret = send_nointr(wb->get_hdr(), wb->get_hdrlen(), sent, 
			  wb->get_totlen() > wb->get_hdrlen() ? MSG_MORE : 0);
	if (ret == -1) {
	    err = EPIPE;
	    goto out;
	}
	sent += ret;
	if (sent != wb->get_hdrlen() && is_synchronous() == false) {
	    while (tmp_poll(fd, PollEvent::POLL_OUT, 
			    std::numeric_limits<int>::max(), 0) == 0) {}
	}
    }
    sent = 0;
    while (sent != wb->get_len()) {
	ret = send_nointr(wb->get_buf(), wb->get_len(), sent, 0);
	if (ret == -1) {
	    err = EPIPE;
	    goto out;
	}
	sent += ret;
	if (sent != wb->get_hdrlen() && is_synchronous() == false) {
	    while (tmp_poll(fd, PollEvent::POLL_OUT, 
			    std::numeric_limits<int>::max(), 0) == 0) {}
	}
    }
out:
    wb->rollback_hdr(hdr.get_raw_len());
    return err;
}

const ReadBuf *TCPTransport::recv()
{
    int ret;

    if (recv_rb)
	recv_rb->release();
    recv_rb = 0;

    while ((ret = recv_nointr(0)) == EAGAIN) {
	while (tmp_poll(fd, PollEvent::POLL_IN, 
			std::numeric_limits<int>::max(), 0) == 0) {}
    }
    if (ret != 0) {
	LOG_INFO(std::string("TCPTransport::recv() ") + ::strerror(ret));
	return 0;
    }
    
    recv_rb = new ReadBuf(recv_buf + TCPTransportHdr::get_raw_len(),
			  recv_buf_offset - TCPTransportHdr::get_raw_len());
    recv_buf_offset = 0;
    return recv_rb;
}

void TCPTransport::set_timeout (int tout /* sec*/)
{
    static const int interval  = 1;
    static const int keepalive = 1;

    if (::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive,
                     sizeof(keepalive)) != 0) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval,
                     sizeof(interval)) != 0) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));	
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &interval,
                     sizeof(interval)) != 0) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));	
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &tout, sizeof(tout)) != 0) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw DFatalException(::strerror(err));	
    }
}
