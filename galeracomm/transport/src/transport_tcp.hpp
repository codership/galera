#ifndef TRANSPORT_TCP_HPP
#define TRANSPORT_TCP_HPP

#include "galeracomm/transport.hpp"
#include "transport_common.hpp"

#include <string>
#include <cerrno>
#include <cstdlib>
#include <cassert>

#include <deque>

#include <sys/socket.h>

class TCPTransport : public Transport, PollContext
{
    int fd;
    int no_nagle;
    std::string peer;
    sockaddr sa;
    size_t sa_size;
    Poll *poll;
    size_t max_pending;
    size_t pending_bytes;
    size_t recv_buf_size;
    unsigned char *recv_buf;
    size_t recv_buf_offset;
    ReadBuf *recv_rb;
    // Used to hold pointer to ReadBuf that has been passed upwards. 
    // If receiver is running in separate thread and thread exits 
    // in handle_up(), reference to pointer is lost.
    ReadBuf *up_rb;
    //boost::crc_32_type send_crc;
    //boost::crc_32_type recv_crc;
    std::deque<PendingWriteBuf> pending;

    long const ka_timeout;
    long const ka_interval;
    long long  last_in;
    long long  last_out;

    TCPTransport (const TCPTransport&);
    void operator= (const TCPTransport&);

    TCPTransport(const int _fd, const sockaddr& _sa, 
		 const size_t _sa_size, Poll *_poll,
                 int tout = Poll::DEFAULT_KA_TIMEOUT);
public:

    TCPTransport(Poll *p,
                 int tout = Poll::DEFAULT_KA_TIMEOUT)
        : 
	fd(-1),
        no_nagle(1),
        peer(""),
        sa(),
        sa_size(0),
        poll(p),
        max_pending(1024),
        pending_bytes(0), 
	recv_buf_size(65536),
	recv_buf (reinterpret_cast<unsigned char*>(::malloc(recv_buf_size))),
	recv_buf_offset(0),
        recv_rb(0),
        up_rb(0),
        pending(),
        ka_timeout (tout),
        ka_interval (Poll::DEFAULT_KA_INTERVAL),
        last_in (PollContext::get_timestamp()),
        last_out (last_in)
    {
	set_max_pending_bytes(1024*1024);
    }

    ~TCPTransport()
    {
	if (fd != -1) {
	    if (poll)
		poll->erase(fd);
            closefd(fd);
	}
	free(recv_buf);
	if (up_rb)
	    up_rb->release();
    }

    size_t get_max_msg_size() const
    {
	return ((1 << 16) - 512); // max IP packet minus some space for headers
    }

    void connect(const char *addr);
    void close();
    void listen(const char *addr);
    Transport *accept(Poll *, Protolay *);

    int recv_nointr(long long);
    int recv_nointr(int, long long);
    ssize_t send_nointr(const void *buf, const size_t buflen, 
			const size_t offset, int flags);

    int handle_down(WriteBuf *, const ProtoDownMeta *);

    // PollContext handle()
    void handle(const int fd, const PollEnum e, long long tstamp);

    int handle_pending(long long tstamp);

    int send(WriteBuf *wb, const ProtoDownMeta *dm);
    const ReadBuf *recv();

    void set_timeout (int tout /* sec*/);
};

#endif // TRANSPORT_TCP_HPP
