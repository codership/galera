
#include "gcomm/transport.hpp"

#include "tcp.hpp"
#include "gmcast.hpp"
#include "evs.hpp"
#include "pc.hpp"
#include "gcomm/conf.hpp"

BEGIN_GCOMM_NAMESPACE

// Private methods

void Transport::set_state(const TransportState state)
{
    this->state = state;
}

// Public methods

bool Transport::supports_uuid() const
{
    return false;
}

const UUID& Transport::get_uuid() const
{
    LOG_FATAL("UUID not supported by " + uri.get_scheme());
    throw FatalException("UUID not supported");
}

std::string Transport::get_remote_url() const
{
    throw FatalException("get remote url not supported");
}

std::string Transport::get_remote_host() const
{
    throw FatalException("get remote host not supported");
}

std::string Transport::get_remote_port() const
{
    throw FatalException("get remote port not supported");
}

TransportState Transport::get_state() const
{
    return state;
}

int Transport::get_errno() const
{
    return error_no;
}

int Transport::get_fd() const
{
    return fd;
}

void Transport::listen()
{
    throw FatalException("not supported");
}

Transport* Transport::accept()
{
    throw FatalException("not supported");
}

int Transport::send(WriteBuf* wb, const ProtoDownMeta* dm)
{
    throw FatalException("Not implemented");
}

const ReadBuf* Transport::recv()
{
    throw FatalException("Not implemented");
}

// CTOR/DTOR

Transport::Transport(const URI& uri_, EventLoop* event_loop_, Monitor* mon_) :
    mon(mon_),
    uri(uri_),
    state(S_CLOSED),
    error_no(0),
    event_loop(event_loop_),
    fd(-1)
{
}

Transport::~Transport()
{

}

// Factory method

static Monitor transport_mon;

Transport* Transport::create(const URI& uri, EventLoop* event_loop)
{
    
    if (uri.get_scheme() == Conf::TcpScheme)
    {
        return new TCP(uri, event_loop, &transport_mon);
    }
    else if (uri.get_scheme() == Conf::GMCastScheme)
    {
        return new GMCast(uri, event_loop, &transport_mon);
    }
    else if (uri.get_scheme() == Conf::EvsScheme)
    {
        return new EVS(uri, event_loop, &transport_mon);
    }
    else if (uri.get_scheme() == Conf::PcScheme)
    {
        return new PC(uri, event_loop, &transport_mon);
    }
    throw FatalException("scheme not supported");
}

Transport* Transport::create(const string& uri_str, EventLoop* el)
{
    return create(URI(uri_str), el);
}

END_GCOMM_NAMESPACE
