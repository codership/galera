#ifndef TRANSPORT_COMMON_HPP
#define TRANSPORT_COMMON_HPP

#include "galeracomm/transport.hpp"
#include "galeracomm/logger.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

class PendingWriteBuf {
public:
    WriteBuf *wb;
    size_t offset;

    PendingWriteBuf(WriteBuf *_wb, size_t _offset) : wb(_wb), offset(_offset) {}

    PendingWriteBuf (const PendingWriteBuf& pwb) :
        wb(pwb.wb), offset(pwb.offset)
    {}

    PendingWriteBuf& operator= (const PendingWriteBuf& pwb)
    {
        wb = pwb.wb; offset = pwb.offset; return *this;
    }

    ~PendingWriteBuf() {
    }
};

inline void closefd(int fd)
{
    while (::close(fd) == -1 && errno == EINTR) {}
}

static inline std::string to_string(const sockaddr* sa)
{
    std::ostringstream ret;
    if (sa->sa_family == AF_INET) {
	ret << ::inet_ntoa(reinterpret_cast<const sockaddr_in*>(sa)->sin_addr)
	    << ":"
	    << gu_be16(reinterpret_cast<const sockaddr_in*>(sa)->sin_port);
    }
    else
	ret << "Unknown address family: " << (sa->sa_family);
    return ret.str();
}


#endif // !TRANSPORT_COMMON_HPP
