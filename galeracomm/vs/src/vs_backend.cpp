
#include "vs_backend.hpp"
#include "vs_fifo_backend.hpp"
#include "vs_remote_backend.hpp"

VSBackend *VSBackend::create(const char *conf, Poll *p, Protolay *up_ctx)
{
    if (strncasecmp(conf, "fifo", 4) == 0)
	return new VSFifoBackend(p, up_ctx);
    if (strncasecmp(conf, "asynctcp:", strlen("asynctcp:")) == 0 ||
	strncasecmp(conf, "tcp:", strlen("tcp:")) == 0)
	return new VSRBackend(p, up_ctx);
    throw DException("Unknown backend");
}
