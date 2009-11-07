/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 */



#include "gu_poll.hpp"
#include "gu_epoll.hpp"
#include "gu_throw.hpp"

#include <cerrno>
#include <string>

using namespace std;

string get_poll_type()
{
    return "epoll";
}

gu::net::Poll* gu::net::Poll::create()
{
    string type(get_poll_type());
    if (type == "epoll")
    {
        return new EPoll();
    }
    gu_throw_error(EINVAL) << "invalid poll type: " << type;
    throw;
}
