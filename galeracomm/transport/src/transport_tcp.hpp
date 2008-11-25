#ifndef TRANSPORT_TCP_HPP
#define TRANSPORT_TCP_HPP

#include "gcomm/transport.hpp"
#include "transport_common.hpp"

#include <cerrno>
#include <cstdlib>
#include <cassert>


#include <deque>

#include <sys/socket.h>



class TCPTransport : public Transport, PollContext {
    int fd;
    int no_nagle;
    sockaddr sa;
    size_t sa_size;
    Poll *poll;
    size_t max_pending;
    size_t pending_bytes;
    unsigned char *recv_buf;
    size_t recv_buf_size;
    size_t recv_buf_offset;
    ReadBuf *recv_rb;
    // Used to hold pointer to ReadBuf that has been passed upwards. 
    // If receiver is running in separate thread and thread exits 
    // in handle_up(), reference to pointer is lost.
    ReadBuf *up_rb;
    //boost::crc_32_type send_crc;
    //boost::crc_32_type recv_crc;

    std::deque<PendingWriteBuf> pending;
    TCPTransport(const int _fd, const sockaddr& _sa, 
		 const size_t _sa_size, Poll *_poll) :
	fd(_fd), no_nagle(1), sa(_sa), sa_size(_sa_size), poll(_poll),
	max_pending(1024), pending_bytes(0), recv_buf_offset(0), recv_rb(0),
	up_rb(0) {
	recv_buf_size = 65536;
	recv_buf = reinterpret_cast<unsigned char*>(::malloc(recv_buf_size));
	set_max_pending_bytes(10*1024*1024);
    }
public:
    TCPTransport(Poll *p) : 
	fd(-1), no_nagle(1), poll(p), max_pending(1024), pending_bytes(0), 
	recv_buf_offset(0), recv_rb(0), up_rb(0) {
	recv_buf_size = 65536;
	recv_buf = reinterpret_cast<unsigned char*>(::malloc(recv_buf_size));
	set_max_pending_bytes(1024*1024);
    }
    ~TCPTransport() {
	if (fd != -1) {
	    if (poll)
		poll->erase(fd);
            closefd(fd);
	}
	free(recv_buf);
	if (up_rb)
	    up_rb->release();
    }

    size_t get_max_msg_size() const {
	return 1024*1024;
    }

    void connect(const char *addr);
    void close();
    void listen(const char *addr);
    Transport *accept(Poll *, Protolay *);

    int recv_nointr();
    int recv_nointr(int);
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
