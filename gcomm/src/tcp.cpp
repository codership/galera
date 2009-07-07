#include "tcp.hpp"

#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"



#include <fcntl.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

BEGIN_GCOMM_NAMESPACE

static inline void closefd(int fd)
{
    while (::close(fd) == -1 && errno == EINTR) {}
}

static bool tcp_addr_to_sa(const char *addr, struct sockaddr *s, size_t *s_size)
{
     char *ipaddr;
     char *port;
     const char *delim;
     
     if (!(delim = strchr(addr, ':')))
     {
         LOG_ERROR("no host-service delimiter");
	 return false;
     }
     ipaddr = strndup(addr, delim - addr);
     port = strdup(delim + 1);

     addrinfo addrhint = {
         0,
         AF_UNSPEC,
         SOCK_STREAM,
         0,
         *s_size,
         s,
         0,
         0
     };
     addrinfo* addri = 0;

     int err;
     if ((err = getaddrinfo(ipaddr, port, &addrhint, &addri)) != 0)
     {
         LOG_ERROR("getaddrinfo: " + make_int(err).to_string());
         return false;
     }
     
     if (addri == 0)
     {
         LOG_ERROR("no address found");
         throw FatalException("");
     }
     
     if (addri->ai_socktype != SOCK_STREAM)
     {
         LOG_FATAL("returned socket is not stream");
         throw FatalException("");
     }
     
     *s = *addri->ai_addr;
     *s_size = addri->ai_addrlen;
     // LOG_WARN(Sockaddr(*s).to_string());
     freeaddrinfo(addri);
     free(ipaddr);
     free(port);
     return true;
}


class TCPHdr
{
    unsigned char raw[sizeof(uint32_t)];
    uint32_t len;
public:
    TCPHdr(const size_t l) : raw(), len(l) 
    {
	if (gcomm::write(len, raw, sizeof(raw), 0) == 0)
	    throw FatalException("");
    }
    TCPHdr(const unsigned char *buf, const size_t buflen, 
           const size_t offset) :
        raw(),
        len()
    {
	if (buflen < sizeof(raw) + offset)
	    throw FatalException("");
	::memcpy(raw, buf + offset, sizeof(raw));
	if (gcomm::read(raw, sizeof(raw), 0, &len) == 0)
	    throw FatalException("");
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



void TCP::connect()
{
    if (fd != -1)
	throw FatalException("TCP::connect(): Already connected or listening");
    if (!tcp_addr_to_sa(uri.get_authority().c_str(), &sa, &sa_size))
	throw FatalException("TCP::connect(): Invalid address");

    set_blocking_mode();

    if ((fd = ::socket(sa.sa_family, SOCK_STREAM, 0)) == -1)
	throw FatalException(::strerror(errno));
    LOG_DEBUG("socket " + make_int(fd).to_string());
    linger lg = {1, 3};
    if (::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == -1) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw FatalException(::strerror(err));
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle, sizeof(no_nagle)) == -1) {
	int err = errno;
        closefd(fd);
	fd = -1;
	throw FatalException(::strerror(err));	
    }

    if (is_non_blocking()) {
	if (::fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
	    int err = errno;
            closefd(fd);
	    fd = -1;
	    throw FatalException(::strerror(err));
	}
    } 
    if (event_loop) {
	event_loop->insert(fd, this);
	event_loop->set(fd, Event::E_IN);
    }
    if (::connect(fd, &sa, sa_size) == -1) 
    {
	if (errno != EINPROGRESS) {
            LOG_ERROR(string("connect(): ") + ::strerror(errno));
	    throw RuntimeException(::strerror(errno));
	} else {
	    if (event_loop)
		event_loop->set(fd, Event::E_OUT);
	    state = S_CONNECTING;
	}
    } else {
	state = S_CONNECTED;
    }
}

void TCP::close()
{
    if (fd != -1) 
    {
        LOG_DEBUG("closing " + make_int(fd).to_string());
	if (event_loop)
	    event_loop->erase(fd);
        closefd(fd);
	fd = -1;
    }
}

void TCP::listen()
{
    LOG_DEBUG(std::string("TCP::listen(") + uri.to_string() + ")");
    if (fd != -1)
	throw FatalException("TCP::listen(): Already connected or listening");
    if (!tcp_addr_to_sa(uri.get_authority().c_str(), &sa, &sa_size))
	throw FatalException("TCP::listen(): Invalid address");
    if ((fd = ::socket(sa.sa_family, SOCK_STREAM, 0)) == -1)
	throw FatalException("TCP::listen(): Could not open socket");
    
    set_blocking_mode();

    if (is_non_blocking() && ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	throw FatalException("TCP::listen(): Fcntl failed");
    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
	throw FatalException("TCP::listen(): Setsockopt faild");
    if (::bind(fd, &sa, sa_size) == -1) {
	int err = errno;
	LOG_FATAL(std::string("TCP::listen(): Bind failed to address ") 
                  + Sockaddr(sa).to_string() + " failed " +
                  ::strerror(err));
	throw FatalException("TCP::listen(): Bind failed");
    }
    if (::listen(fd, 128) == -1)
	throw FatalException("TCP::listen(): Listen failed");
    if (event_loop) {
	event_loop->insert(fd, this);
	event_loop->set(fd, Event::E_IN);
    }
    state = S_LISTENING;
}

Transport *TCP::accept()
{


    sockaddr sa;
    socklen_t sa_size = sizeof(sockaddr);
    
    int acc_fd;
    if ((acc_fd = ::accept(fd, &sa, &sa_size)) == -1)
	throw FatalException("TCP::accept(): Accept failed");
    LOG_DEBUG(std::string("accept()") + make_int(acc_fd).to_string());

    if (is_non_blocking() && ::fcntl(acc_fd, F_SETFL, O_NONBLOCK) == -1) {
        closefd(fd);
	throw FatalException("TCP::accept(): Fcntl failed");
    }

    linger lg = {1, 3};
    if (::setsockopt(acc_fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == -1) 
    {
        closefd(acc_fd);
	acc_fd = -1;
	throw FatalException("TCP::accept(): set linger failed");
    }
    
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle, sizeof(no_nagle)) == -1) {
	int err = errno;
        closefd(fd);
	throw FatalException(::strerror(err));	
    }
    
    TCP *ret = new TCP(uri, event_loop, mon);
    ret->fd = acc_fd;
    ret->state = S_CONNECTED;
    
    if (event_loop)
    {
	event_loop->insert(ret->fd, ret);
	event_loop->set(ret->fd, Event::E_IN);
    }
    return ret;
}

//
// Send stuff while masking out interrupted system calls. 
// Sends buflen - offset bytes, unless send fails with EAGAIN. 
// In case of EAGAIN, short byte count is returned.
//
//

ssize_t TCP::send_nointr(const void *buf, const size_t buflen, 
				  const size_t offset, int flags)
{
    ssize_t ret;
    ssize_t sent = 0;
    
    flags |= MSG_NOSIGNAL;

    if (buflen == offset)
	return 0;
    do {
	do {
	    ret = ::send(fd, (unsigned char *)buf + offset + sent, 
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

class DummyEventContext : public EventContext {
    void handle_event(const int fd, const Event& pe) {
	// 
    }
};

static int tmp_poll(int fd, const int pe, int tout, EventContext *ctx)
{
    DummyEventContext dummy;
    EventLoop el;
    LOG_DEBUG("tmp_poll");
    el.insert(fd, ctx ? ctx : &dummy);
    el.set(fd, pe);
    int ret = el.poll(tout);
    return ret;
}

void TCP::handle_up(int cid, const ReadBuf* rb, const size_t roff, const ProtoUpMeta* um)
{
    throw FatalException("not supported");
}


int TCP::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    if (state != S_CONNECTED)
	return ENOTCONN;
    if (is_non_blocking() == false)
    {
        LOG_TRACE("");
	return TCP::send(wb, dm);
    }
    
    if (pending_bytes + wb->get_totlen() > max_pending_bytes)
    {
	/* was: pending.size() == max_pending */
	LOG_DEBUG("TCP::handle_down(): Contention");
	if (contention_tries > 0 ) {
	    for (long i = 0; 
                 i < contention_tries && 
		     pending_bytes + wb->get_totlen() > 
		     max_pending_bytes; i++) {
		tmp_poll(fd, Event::E_OUT, contention_tout, this);
	    }
	}
        
	if (pending_bytes + wb->get_totlen() > max_pending_bytes)
        {
	    LOG_DEBUG("contention not cleared: pending " 
                      + make_int(pending_bytes).to_string()
                      + " max pending " 
                      + make_int(max_pending_bytes).to_string());
	    return EAGAIN;
	}
    }
    
    TCPHdr hdr(wb->get_totlen());
    wb->prepend_hdr(hdr.get_raw(), hdr.get_raw_len());
    
    if (pending.size()) {
	WriteBuf *wb_copy = wb->copy();
	pending.push_back(PendingWriteBuf(wb_copy, 0));
	pending_bytes += wb->get_totlen();
	LOG_DEBUG("TCP::handle_down(): Appended to pending");
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
	    if (event_loop)
		event_loop->set(fd, Event::E_OUT);
	    LOG_DEBUG("TCP::handle_down(): Appended to pending in header send");
	    goto out_success;
	}
	
	if ((ret = send_nointr(wb->get_buf(), wb->get_len(), 0, 0)) == -1)
	    goto out_epipe;
	
	
	sent += ret;
	if (sent != wb->get_len() + wb->get_hdrlen()) {
	    WriteBuf *wb_copy = wb->copy();
	    pending.push_back(PendingWriteBuf(wb_copy, sent));
	    pending_bytes += wb->get_totlen();
	    if (event_loop)
		event_loop->set(fd, Event::E_OUT);
	    LOG_DEBUG("TCP::handle_down(): Appended to pending in payload send");
	}
    }
    
out_success:
    wb->rollback_hdr(hdr.get_raw_len());
    return 0;
    
out_epipe:
    LOG_WARN("TCP::handle_down(): Broken pipe");
    wb->rollback_hdr(hdr.get_raw_len());
    return EPIPE;
}


int TCP::recv_nointr(int flags)
{
    
    if (recv_buf_offset < TCPHdr::get_raw_len()) {
	ssize_t ret;    
	do {
	    ret = ::recv(fd, recv_buf + recv_buf_offset, 
			 TCPHdr::get_raw_len() - recv_buf_offset,
			 flags);
	} while (ret == -1 && errno == EINTR);
	if (ret == -1 && errno == EAGAIN) {
	    LOG_DEBUG("TCP::recv_nointr(): Return EAGAIN in header recv"); 
	    return EAGAIN;
	}
	else if (ret == -1 || ret == 0) {
	    LOG_DEBUG("TCP::recv_nointr(): Return error in header recv"); 
	    return ret == -1 ? errno : EPIPE;
	}
	recv_buf_offset += ret;
	if (recv_buf_offset < TCPHdr::get_raw_len()) {
	    LOG_DEBUG("TCP::recv_nointr(): Return EAGAIN in header recv"); 
	    return EAGAIN;
	}
    }
    
    TCPHdr hdr(recv_buf, recv_buf_offset, 0);
    
    if (recv_buf_size < hdr.get_len() + hdr.get_raw_len()) {
	recv_buf = reinterpret_cast<unsigned char*>(
	    ::realloc(recv_buf, hdr.get_len() + hdr.get_raw_len()));
    }
    
    if (hdr.get_len() == 0) {
	LOG_DEBUG("TCP::recv_nointr(): Zero len message"); 
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
	    LOG_DEBUG("TCP::recv_nointr(): Return EAGAIN in body recv"); 
	    return EAGAIN;
	}
	else if (ret == -1 || ret == 0) {
	    LOG_DEBUG("TCP::recv_nointr(): Return error in body recv"); 
	    return ret == -1 ? errno : EPIPE;
	}
	recv_buf_offset += ret;
    } while (recv_buf_offset < hdr.get_len() + hdr.get_raw_len());
    return 0;
}

int TCP::recv_nointr()
{
    return recv_nointr(MSG_DONTWAIT);
}

int TCP::handle_pending()
{
    LOG_DEBUG("enter");
    if (pending.size() == 0)
    {
        LOG_DEBUG("return 0");
	return 0;
    }
    std::deque<PendingWriteBuf>::iterator i;
    while (pending.size())
    {
	i = pending.begin();
	ssize_t ret = 0;
	if (i->offset < i->wb->get_hdrlen()) {
	    if ((ret = send_nointr(i->wb->get_hdr(), 
				   i->wb->get_hdrlen(), i->offset, 
				   i->wb->get_len() ? MSG_MORE : 0)) == -1)
            {
                LOG_DEBUG("return EPIPE");
		return EPIPE;
            }
	    i->offset += ret;
	    if (i->offset != i->wb->get_hdrlen())
            {
		LOG_DEBUG("return EAGAIN");
		return EAGAIN;
	    }
	}
	ret = 0;
	if (i->wb->get_len() &&
	    (ret = send_nointr(i->wb->get_buf(), i->wb->get_len(),
			       i->offset - i->wb->get_hdrlen(), 0)) == -1)
        {
            LOG_DEBUG("return EPIPE");
	    return EPIPE;
	}
	i->offset += ret;
	if (i->offset != i->wb->get_totlen())
        {
	    LOG_DEBUG("return EAGAIN");
	    return EAGAIN;
	}
	
	pending_bytes -= i->offset;
	delete i->wb;
	pending.pop_front();
    }
    LOG_DEBUG("return 0");
    
    return 0;
}

void TCP::handle_event(const int fd, const Event& pe)
{
    if (fd != this->fd)
    {
        throw FatalException("");
    }
    
    if (pe.get_cause() & Event::E_HUP)
    {
	this->error_no = ENOTCONN;
	state = S_FAILED;
	pass_up(0, 0, 0);
        return;
    }
    else if (pe.get_cause() & Event::E_ERR)
    {
	int err = 0;
	socklen_t errlen = sizeof(err);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) 
        {
	    LOG_WARN("TCP::handle(): Poll error");
	}
	this->error_no = ENOTCONN;
	state = S_FAILED;
	pass_up(0, 0, 0);
        return;
    }
    else if (pe.get_cause() & Event::E_OUT)
    {
	LOG_DEBUG("TCP::handle(): Event::E_OUT");
	int ret;
	if (state == S_CONNECTING) {
	    int err = 0;
	    socklen_t errlen = sizeof(err);
            if (event_loop)
                event_loop->unset(fd, Event::E_OUT);
	    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1)
		throw FatalException("TCP::handle(): Getsockopt failed");
	    if (err == 0) {
		state = S_CONNECTED;
	    } else {
		this->error_no = err;
		state = S_FAILED;
	    }
	    pass_up(0, 0, 0);
            return;
	} else if ((ret = handle_pending()) == 0) {
            if (event_loop)
                event_loop->unset(fd, Event::E_OUT);
	} else if (ret != EAGAIN) {
	    // Something bad has happened
	    this->error_no = ret;
	    state = S_FAILED;
	    LOG_DEBUG("TCP::handle(): Failed");
	    pass_up(0, 0, 0);
            return;
	}
    }
    else if (pe.get_cause() & Event::E_IN) 
    {
	if (state == S_CONNECTED) {
	    ssize_t ret = recv_nointr();
	    if (ret == 0) {
		ReadBuf rb(recv_buf, recv_buf_offset, true);
		pass_up(&rb, TCPHdr::get_raw_len(), 0);
		recv_buf_offset = 0;
	    } else if (ret != EAGAIN) {
		this->error_no = ret;
		state = S_FAILED;
		LOG_DEBUG("TCP::handle(): Failed");
		pass_up(0, 0, 0);
                return;
	    }
	} else if (state == S_LISTENING) {
	    pass_up(0, 0, 0);
	}
    }
    else if (pe.get_cause() & Event::E_INVAL)
    {
	throw FatalException("TCP::handle(): Invalid file descriptor");
    }
    else
    {
        LOG_FATAL("unhnadled event");
    }
}





int TCP::send(WriteBuf *wb, const ProtoDownMeta *dm)
{

    TCPHdr hdr(wb->get_totlen());
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
	if (sent != wb->get_hdrlen() && is_non_blocking()) {
	    while (tmp_poll(fd, Event::E_OUT, 
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
	if (sent != wb->get_hdrlen() && is_non_blocking()) {
	    while (tmp_poll(fd, Event::E_OUT, 
			    std::numeric_limits<int>::max(), 0) == 0) {}
	}
    }
out:
    wb->rollback_hdr(hdr.get_raw_len());
    return err;
}

const ReadBuf *TCP::recv()
{
    int ret;

    if (recv_rb)
	recv_rb->release();
    recv_rb = 0;

    while ((ret = recv_nointr(0)) == EAGAIN) {
	while (tmp_poll(fd, Event::E_IN, 
			std::numeric_limits<int>::max(), 0) == 0) {}
    }
    if (ret != 0) {
	LOG_DEBUG(std::string("TCP::recv() ") + ::strerror(ret));
	return 0;
    }
    
    recv_rb = new ReadBuf(recv_buf + TCPHdr::get_raw_len(),
			  recv_buf_offset - TCPHdr::get_raw_len());
    recv_buf_offset = 0;
    return recv_rb;
}

END_GCOMM_NAMESPACE
