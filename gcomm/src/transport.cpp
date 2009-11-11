
#include "gcomm/transport.hpp"
#include "socket.hpp"
#include "gmcast.hpp"
#include "pc.hpp"
#include "gcomm/conf.hpp"

using namespace std;

using namespace gu;

// Private methods

void gcomm::Transport::set_state(const State state)
{
    this->state = state;
}

// Public methods

bool gcomm::Transport::supports_uuid() const
{
    return false;
}

const gcomm::UUID& gcomm::Transport::get_uuid() const
{
    gu_throw_fatal << "UUID not supported by " + uri.get_scheme();
    throw;
}

string gcomm::Transport::get_local_addr() const
{
    gu_throw_fatal << "get local url not supported";
    throw;
}

string gcomm::Transport::get_remote_addr() const
{
    gu_throw_fatal << "get remote url not supported";
    throw;
}



gcomm::Transport::State gcomm::Transport::get_state() const
{
    return state;
}

int gcomm::Transport::get_errno() const
{
    return error_no;
}

int gcomm::Transport::get_fd() const
{
    return -1;
}

void gcomm::Transport::listen()
{
    gu_throw_fatal << "not supported";
}

gcomm::Transport* gcomm::Transport::accept()
{
    gu_throw_fatal << "not supported";
    throw;
}


// CTOR/DTOR

gcomm::Transport::Transport(Protonet& pnet_, const URI& uri_) :
    pstack(),
    pnet(pnet_),
    uri(uri_),
    state(S_CLOSED),
    error_no(0)
{ }

gcomm::Transport::~Transport() {}


gcomm::Transport* 
gcomm::Transport::create(Protonet& pnet, const string& uri_str)
{
    const URI uri(uri_str);
    const std::string& scheme = uri.get_scheme();
    
    if (scheme == Conf::TcpScheme)
    {
        return new Socket(pnet, uri_str);
    }
    else if (scheme == Conf::GMCastScheme)
    {
        return new GMCast(pnet, uri_str);
    }
    else if (scheme == Conf::PcScheme)
    {
        return new PC(pnet, uri_str);
    }
    
    gu_throw_fatal << "scheme not supported";
    
    throw; // to make compiler happy
}



