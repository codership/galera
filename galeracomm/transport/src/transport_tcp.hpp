#ifndef TRANSPORT_TCP_HPP
#define TRANSPORT_TCP_HPP

#include "gcomm/transport.hpp"


#include <cerrno>
#include <cstdlib>
#include <cassert>


#include <deque>

#include <sys/socket.h>



class PendingWriteBuf {
public:
    WriteBuf *wb;
    size_t offset;
    PendingWriteBuf(WriteBuf *_wb, size_t _offset) : wb(_wb), offset(_offset)
	{}
    ~PendingWriteBuf() {
    }
};

class TCPTransport : public Transport, PollContext {
    int fd;
    sockaddr sa;
    size_t sa_size;
    Poll *poll;
    size_t max_pending;
    size_t pending_bytes;
    unsigned char *recv_buf;
    size_t recv_buf_size;
    size_t recv_buf_offset;
    ReadBuf *recv_rb;
    //boost::crc_32_type send_crc;
    //boost::crc_32_type recv_crc;

    std::deque<PendingWriteBuf> pending;
    TCPTransport(const int _fd, const sockaddr& _sa, 
		 const size_t _sa_size, Poll *_poll) :
	fd(_fd), sa(_sa), sa_size(_sa_size), poll(_poll),
	max_pending(1024), pending_bytes(0), recv_buf_offset(0), recv_rb(0) {
	
	recv_buf_size = 65536;
	recv_buf = reinterpret_cast<unsigned char*>(::malloc(recv_buf_size));
	set_max_pending_bytes(10*1024*1024);
    }
public:
    TCPTransport(Poll *p) : 
	fd(-1), poll(p), max_pending(1024), pending_bytes(0), 
	recv_buf_offset(0), recv_rb(0) {
	recv_buf_size = 65536;
	recv_buf = reinterpret_cast<unsigned char*>(::malloc(recv_buf_size));
	set_max_pending_bytes(1024*1024);
    }
    ~TCPTransport() {
	if (fd != -1) {
	    if (poll)
		poll->erase(fd);
	    while (::close(fd) == -1 && errno == EINTR);
	}
	free(recv_buf);
    }
    void connect(const char *addr);
    void close();
    void listen(const char *addr);
    Transport *accept(Poll *, Protolay *);

    int recv_nointr();
    ssize_t send_nointr(const void *buf, const size_t buflen, 
			const size_t offset, int flags);

    int handle_down(WriteBuf *, const ProtoDownMeta *);

    // PollContext handle()
    void handle(const int, const PollEnum);

    int handle_pending();

    int send(WriteBuf *wb, const ProtoDownMeta *dm);
    const ReadBuf *recv();

};

#endif // TRANSPORT_TCP_HPP
