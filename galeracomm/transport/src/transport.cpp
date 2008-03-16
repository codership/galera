
#include <gcomm/transport.hpp>
#include <gcomm/exception.hpp>

#include <cstring>


#include "transport_tcp.hpp"


Transport *Transport::create(const char *type, Poll *poll, 
			     Protolay *up_ctx)
{
    Transport *ret = 0;
    if (strncmp(type, "tcp:", strlen("tcp:")) == 0) {
	ret = new TCPTransport(poll);
	if (up_ctx)
	    ret->set_up_context(up_ctx);
    }

    return ret;
}

Transport *Transport::create(const char *type, Poll *poll)
{
    return create(type, poll, 0);
}
