
#include <gcomm/transport.hpp>
#include <gcomm/exception.hpp>

#include <cstring>


#include "transport_tcp.hpp"
#include "transport_tipc.hpp"

#include <linux/tipc.h>

const Sockaddr Sockaddr::ADDR_INVALID(0xffU);
const Sockaddr Sockaddr::ADDR_ANY(0);

Transport *Transport::create(const char *type, Poll *poll, 
			     Protolay *up_ctx)
{
    Transport *ret = 0;
    if (strncmp(type, "tcp:", strlen("tcp:")) == 0 ||
	strncmp(type, "asynctcp:", strlen("asynctcp:")) == 0) {
	ret = new TCPTransport(poll);
    } else if (strncmp(type, "tipc:", strlen("tipc:")) == 0) {
	ret = new TIPCTransport(poll);
    } else {
	throw FatalException("Unknown transport type");
    }
    if (up_ctx)
	ret->set_up_context(up_ctx);    
    return ret;
}

Transport *Transport::create(const char *type, Poll *poll)
{
    return create(type, poll, 0);
}
