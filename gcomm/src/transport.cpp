/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#include "gcomm/transport.hpp"
#include "socket.hpp"
#include "gmcast.hpp"
#include "pc.hpp"
#include "gcomm/conf.hpp"

// Public methods

const gcomm::UUID& gcomm::Transport::uuid() const
{
    gu_throw_fatal << "UUID not supported by " + uri_.get_scheme();
}

std::string gcomm::Transport::local_addr() const
{
    gu_throw_fatal << "get local url not supported";
}

std::string gcomm::Transport::remote_addr() const
{
    gu_throw_fatal << "get remote url not supported";
}


int gcomm::Transport::err_no() const
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
}


// CTOR/DTOR

gcomm::Transport::Transport(Protonet& pnet, const gu::URI& uri)
    :
    Protolay(pnet.conf()),
    pstack_(),
    pnet_(pnet),
    uri_(uri),
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
}


gcomm::Transport*
gcomm::Transport::create(Protonet&          pnet,
                         const std::string& uri_str)
{
    return create(pnet, gu::URI(uri_str));
}
