#include "tcp.hpp"
#include "defaults.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/types.hpp"
#include "gcomm/util.hpp"


#include <fcntl.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

using std::string;

BEGIN_GCOMM_NAMESPACE

TCP::TCP (const URI& uri_,
          EventLoop* event_loop_,
          Monitor*   mon)
    throw (RuntimeException)
    : 
    Transport         (uri_, event_loop_, mon), 
    no_nagle          (1),
    sa                (),
    sa_size           (),
    recv_buf_size     (65536),
    recv_buf          (reinterpret_cast<byte_t*>(::malloc(recv_buf_size))),
    recv_buf_offset   (0),
    max_pending_bytes (4*1024*1024), // do we really want it that big?
    pending_bytes     (0),
    contention_tries  (30),
    contention_tout   (10),
    recv_rb           (0),
    up_rb             (0),
    pending           (),
    non_blocking      (false) 
{
    if (!recv_buf)
    {
        gcomm_throw_runtime (ENOMEM) << "Failed to allocate "
                                     << recv_buf_size
                                     << " bytes for recv_buf";
    }

#if 0 // with get_query_list       
    const URIQueryList& ql(uri.get_query_list());
    URIQueryList::const_iterator i = ql.find(Conf::TcpParamMaxPending);

    if (i != ql.end())
    {
        max_pending_bytes = read_long(get_query_value(i));
        log_debug << "max_pending_bytes: " << max_pending_bytes;
    }
#else // with get option
    try
    {
        std::string val = uri.get_option (Conf::TcpParamMaxPending);

        try
        {
            max_pending_bytes = gu::from_string<long>(val);
            log_debug << "max_pending_bytes: " << max_pending_bytes;
        }
        catch (gu::NotFound&)
        {
            gcomm_throw_runtime (EINVAL) << "Invalid "
                                         << Conf::TcpParamMaxPending
                                         << " value '" << val << "'";
        }
    }
    catch (gu::NotFound&) { /* no max_pending_bytes option spec'ed */ }
#endif

    try
    {
        std::string val = uri.get_option (Conf::TcpParamNonBlocking);

        try
        {
            non_blocking = gu::from_string<bool>(val);

            if (non_blocking)
            {
                log_debug << "Using non-blocking mode for " << uri.to_string();
            }
        }
        catch (gu::NotFound&)
        {
            gcomm_throw_runtime (EINVAL) << "Invalid "
                                         << Conf::TcpParamNonBlocking
                                         << " value '" << val << "'";
        }
    }
    catch (gu::NotFound&) { /* No non-blocking option slecified */ }
}

static inline void closefd(int fd)
{
    while (::close(fd) == -1 && errno == EINTR) {}
}

static void uri_to_sa(const URI& uri, struct sockaddr *s, size_t *s_size)
    throw (gu::Exception)
{
    const char* host = 0;
    const char* port = 0;

    try
    { 
        host = uri.get_host().c_str();

        if ('\0' == host[0]) host = 0;

        try { port = uri.get_port().c_str(); }
        catch (gu::NotSet&)
        {
            port = Defaults::Port.c_str();
        }
    }
    catch (gu::NotSet&)
    {
        gcomm_throw_runtime (EINVAL) << "URL " << uri.to_string()
                                     << " does not have host field";
    }

    addrinfo addrhint = {
        0,
        AF_UNSPEC,
        SOCK_STREAM,
        0,
        0,
        0,
        0,
        0
    };

    addrinfo* addri = 0;    
    int       err;

    log_debug << "Calling getaddrinfo('" << host << "', '" << port << "', "
              << addrhint.ai_addrlen << ", " << &addri << ')';

    if ((err = getaddrinfo(host, port, &addrhint, &addri)) != 0)
    {
        if (EAI_SYSTEM == err)
        {
            gcomm_throw_runtime(errno) << "System error: " << errno
                                       << " (" << strerror(errno) << ')';
        }
        else
        {
            gcomm_throw_runtime(err) << "getaddrinfo failed: " << err
                                     << " (" << gai_strerror(err) << ')';
        }
    }
    
    if (addri == 0)
    {
        gcomm_throw_fatal << "no address found";
    }
    
    if (addri->ai_socktype != SOCK_STREAM)
    {
        gcomm_throw_fatal << "returned socket is not stream";
    }
    
    *s = *addri->ai_addr;
    *s_size = addri->ai_addrlen;
    freeaddrinfo(addri);
}


class TCPHdr
{
    unsigned char raw[sizeof(uint32_t)];
    uint32_t len;

public:

    TCPHdr(const size_t l) : raw(), len(l) 
    {
	if (gcomm::write(len, raw, sizeof(raw), 0) == 0)
	    gcomm_throw_fatal;
    }

    TCPHdr(const unsigned char *buf, const size_t buflen, 
           const size_t offset) :
        raw(),
        len()
    {
	if (buflen < sizeof(raw) + offset) gcomm_throw_fatal;

	::memcpy(raw, buf + offset, sizeof(raw));

	if (gcomm::read(raw, sizeof(raw), 0, &len) == 0) gcomm_throw_fatal;
    }

    const void*   get_raw()                 const { return raw; }

    const void*   get_raw(const size_t off) const { return raw + off; }

    size_t        get_len()                 const { return len; }

    static size_t get_raw_len()
    {
        return sizeof((reinterpret_cast<TCPHdr*>(0))->raw);
    }
};

void TCP::connect()
{
    if (fd != -1) gcomm_throw_runtime(EISCONN);

    uri_to_sa(uri, &sa, &sa_size);

//    set_blocking_mode();

    if ((fd = ::socket(sa.sa_family, SOCK_STREAM, 0)) == -1)
	gcomm_throw_runtime(errno);

    log_debug << "socket: " << fd;

    linger lg = {1, 3};

    if (::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == -1)
    {
        closefd(fd);
	fd = -1;
	gcomm_throw_runtime(errno);
    }

    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle, sizeof(no_nagle))
        == -1)
    {
        closefd(fd);
	fd = -1;
	gcomm_throw_runtime(errno);
    }

    if (is_non_blocking())
    {
	if (::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
        {
            closefd(fd);
	    fd = -1;
            gcomm_throw_runtime(errno);
	}
    }

    if (event_loop)
    {
	event_loop->insert(fd, this);
	event_loop->set(fd, Event::E_IN);
    }

    if (::connect(fd, &sa, sa_size) == -1) 
    {
	if (errno != EINPROGRESS)
        {
	    gcomm_throw_runtime(errno);
	}
        else
        {
	    if (event_loop) event_loop->set(fd, Event::E_OUT);

	    state = S_CONNECTING;
	}
    }
    else
    {
	state = S_CONNECTED;
    }
}

void TCP::close()
{
    if (fd != -1) 
    {
        log_debug << "closing " << fd;

	if (event_loop) event_loop->erase(fd);

        closefd(fd);
	fd = -1;
    }
}

void TCP::listen()
{
    log_debug << "TCP::listen(" << uri.to_string() << ')';

    if (fd != -1) gcomm_throw_runtime (EISCONN);

    uri_to_sa(uri, &sa, &sa_size);

    if ((fd = ::socket(sa.sa_family, SOCK_STREAM, 0)) == -1)
	gcomm_throw_runtime(errno);

//    set_blocking_mode();

    if (is_non_blocking() && ::fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	gcomm_throw_runtime(errno);

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
	gcomm_throw_runtime(errno);

    if (::bind(fd, &sa, sa_size) == -1)
    {
	gcomm_throw_runtime(errno) << "Bind failed to address " 
                                   << sockaddr_to_uri(Conf::TcpScheme, &sa);
    }

    if (::listen(fd, 128) == -1)
        gcomm_throw_runtime(errno) << "Listen failed: ";

    if (event_loop) {
	event_loop->insert(fd, this);
	event_loop->set(fd, Event::E_IN);
    }

    state = S_LISTENING;
}

Transport *TCP::accept()
{
    sockaddr  acc_sa;
    socklen_t acc_sa_size = sizeof(sockaddr);
    
    int acc_fd;

    if ((acc_fd = ::accept(fd, &acc_sa, &acc_sa_size)) == -1)
	gcomm_throw_runtime(errno) << "Accept failed";

    log_debug << "accept(): " << acc_fd;

    if (is_non_blocking() && ::fcntl(acc_fd, F_SETFL, O_NONBLOCK) == -1) {
        closefd(acc_fd);
	gcomm_throw_runtime(errno) << "Fcntl failed";
    }

    linger lg = {1, 3};
    if (::setsockopt(acc_fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)) == -1) 
    {
        closefd(acc_fd);
	acc_fd = -1;
	gcomm_throw_runtime(errno) << "Setsockopt linger failed";
    }
    
    if (::setsockopt(acc_fd, IPPROTO_TCP, TCP_NODELAY, &no_nagle,
                     sizeof(no_nagle)) == -1) {
        closefd(acc_fd);
	gcomm_throw_runtime(errno);	
    }
    
    TCP *ret = new TCP(uri, event_loop, mon);

    ret->fd      = acc_fd;
    ret->state   = S_CONNECTED;
    ret->sa      = acc_sa;
    ret->sa_size = acc_sa_size;

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

    if (buflen == offset) return 0;

    do {
	do
        {
	    ret = ::send(fd,
                         reinterpret_cast<const char*>(buf) + offset + sent, 
			 buflen - offset - sent,
                         flags);
	}
        while (ret == -1 && errno == EINTR);

	if (ret == -1 && errno == EAGAIN)
        {
	    return sent;

	}
        else if (ret == -1 || ret == 0)
        {
	    return -1;
	}

	sent += ret;
    }
    while (size_t(sent) + offset < buflen);

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
    gcomm_throw_runtime (ENOSYS) << "not supported";
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

void TCP::handle_event (const int fd, const Event& pe)
{
    if (fd != this->fd) gcomm_throw_fatal;
    
    if (pe.get_cause() & Event::E_HUP)
    {
	this->error_no = ENOTCONN;
	state = S_FAILED;
	pass_up(0, 0, 0);
        return;
    }

    if (pe.get_cause() & Event::E_ERR)
    {
	int err          = 0;
	socklen_t errlen = sizeof(err);

	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) 
        {
	    log_warn << "TCP::handle(): Poll error";
	}

	this->error_no = ENOTCONN;
	state = S_FAILED;
	pass_up(0, 0, 0);
        return;
    }

    if (pe.get_cause() & Event::E_OUT)
    {
	log_debug << "TCP::handle(): Event::E_OUT";

	int ret;

	if (state == S_CONNECTING)
        {
	    int err = 0;
	    socklen_t errlen = sizeof(err);

            if (event_loop) event_loop->unset(fd, Event::E_OUT);

	    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1)
		gcomm_throw_runtime(errno) << "Getsockopt failed";

	    if (err == 0)
            {
		state = S_CONNECTED;
	    }
            else
            {
		this->error_no = err;
		state = S_FAILED;
	    }

	    pass_up(0, 0, 0);
            return;
	}

        if ((ret = handle_pending()) == 0)
        {
            if (event_loop) event_loop->unset(fd, Event::E_OUT);
	}
        else if (ret != EAGAIN)
        {
	    // Something bad has happened
	    this->error_no = ret;
	    state = S_FAILED;
	    log_debug << "TCP::handle() failed: " << strerror(ret);
	    pass_up(0, 0, 0);
            return;
	}
    }
//    else if (pe.get_cause() & Event::E_IN) - I think we should have OR here
    if (pe.get_cause() & Event::E_IN) 
    {
	if (state == S_CONNECTED)
        {
	    ssize_t ret = recv_nointr();

	    if (ret == 0)
            {
		ReadBuf rb(recv_buf, recv_buf_offset, true);
		gu_trace(pass_up(&rb, TCPHdr::get_raw_len(), 0));
		recv_buf_offset = 0;
	    }
            else if (ret != EAGAIN)
            {
		this->error_no = ret;
		state = S_FAILED;
		log_warn << "TCP::handle(): Failed";
		pass_up(0, 0, 0);
                return;
	    }
	}
        else if (state == S_LISTENING)
        {
	    pass_up(0, 0, 0);
	}
    }
    else if (pe.get_cause() & Event::E_INVAL)
    {
	gcomm_throw_fatal << "TCP::handle(): Invalid file descriptor";
    }
    else
    {
        log_fatal << "unhnadled event";
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



string TCP::get_remote_url() const
{
    return sockaddr_to_uri(Conf::TcpScheme, &sa);
}

string TCP::get_remote_host() const
{
    return sockaddr_host_to_str(&sa);
}

string TCP::get_remote_port() const
{
    return sockaddr_port_to_str(&sa);
}

END_GCOMM_NAMESPACE
