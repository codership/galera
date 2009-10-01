
#include "gcomm/transport.hpp"

#include "tcp.hpp"
#include "gmcast.hpp"
#include "evs.hpp"
#include "pc.hpp"
#include "gcomm/conf.hpp"

using std::string;

BEGIN_GCOMM_NAMESPACE

// Private methods

void Transport::set_state(const State state)
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
    gcomm_throw_fatal << "UUID not supported by " + uri.get_scheme();
    throw;
}

std::string Transport::get_remote_url() const
{
    gcomm_throw_fatal << "get remote url not supported";
    throw;
}

std::string Transport::get_remote_host() const
{
    gcomm_throw_fatal << "get remote host not supported";
    throw;
}

std::string Transport::get_remote_port() const
{
    gcomm_throw_fatal << "get remote port not supported";
    throw;
}

Transport::State Transport::get_state() const
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
    gcomm_throw_fatal << "not supported";
}

Transport* Transport::accept()
{
    gcomm_throw_fatal << "not supported";
    throw;
}

int Transport::send(WriteBuf* wb, const ProtoDownMeta* dm)
{
    gcomm_throw_fatal << "Not implemented";
    throw;
}

const ReadBuf* Transport::recv()
{
    gcomm_throw_fatal << "Not implemented";
    throw;
}

// CTOR/DTOR

Transport::Transport(const URI& uri_, EventLoop* event_loop_, Monitor* mon_) :
    mon(mon_),
    uri(uri_),
    state(S_CLOSED),
    error_no(0),
    event_loop(event_loop_),
    fd(-1)
{}

Transport::~Transport() {}

// Factory method

static Monitor transport_mon;

Transport* Transport::create(const URI& uri, EventLoop* event_loop)
{
    const std::string& scheme = uri.get_scheme();

    if      (scheme == Conf::TcpScheme)
    {
        return new TCP(uri, event_loop, &transport_mon);
    }
    else if (scheme == Conf::GMCastScheme)
    {
        return new GMCast(uri, event_loop, &transport_mon);
    }
    else if (scheme == Conf::EvsScheme)
    {
        return new EVS(uri, event_loop, &transport_mon);
    }
    else if (scheme == Conf::PcScheme)
    {
        return new PC(uri, event_loop, &transport_mon);
    }

    gcomm_throw_fatal << "scheme not supported";

    throw; // to make compiler happy
}

Transport* Transport::create(const string& uri_str, EventLoop* el)
{
    return create(URI(uri_str), el);
}

END_GCOMM_NAMESPACE
