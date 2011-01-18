/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/transport.hpp"
#include "socket.hpp"
#include "gmcast.hpp"
#include "pc.hpp"
#include "gcomm/conf.hpp"


// Private methods

void gcomm::Transport::set_state(const State state)
{
    state_ = state;
}

// Public methods

bool gcomm::Transport::supports_uuid() const
{
    return false;
}

const gcomm::UUID& gcomm::Transport::get_uuid() const
{
    gu_throw_fatal << "UUID not supported by " + uri_.get_scheme();
    throw;
}

std::string gcomm::Transport::get_local_addr() const
{
    gu_throw_fatal << "get local url not supported";
    throw;
}

std::string gcomm::Transport::get_remote_addr() const
{
    gu_throw_fatal << "get remote url not supported";
    throw;
}



gcomm::Transport::State gcomm::Transport::get_state() const
{
    return state_;
}

int gcomm::Transport::get_errno() const
{
    return error_no_;
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

gcomm::Transport::Transport(Protonet& pnet, const gu::URI& uri)
    :
    Protolay(pnet.conf()),
    pstack_(),
    pnet_(pnet),
    uri_(uri),
    state_(S_CLOSED),
    error_no_(0)
{ }


gcomm::Transport::~Transport()
{ }


gcomm::Transport*
gcomm::Transport::create(Protonet& pnet, const gu::URI& uri)
{

    const std::string& scheme = uri.get_scheme();

    if (scheme == Conf::GMCastScheme)
    {
        return new GMCast(pnet, uri);
    }
    else if (scheme == Conf::PcScheme)
    {
        return new PC(pnet, uri);
    }

    gu_throw_fatal << "scheme '" << uri.get_scheme() << "' not supported";

    throw; // to make compiler happy
}


gcomm::Transport*
gcomm::Transport::create(Protonet& pnet, const std::string& uri_str)
{
    return create(pnet, gu::URI(uri_str));
}
