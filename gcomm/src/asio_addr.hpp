/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_ADDR_HPP
#define GCOMM_ASIO_ADDR_HPP

#include "gu_exception.hpp"

#include "asio_protonet.hpp"

#include <string>
#include <algorithm>

static inline std::string escape_addr(const asio::ip::address& addr)
{
    if (addr.is_v4())
    {
        return addr.to_v4().to_string();
    }
    else
    {
        return "[" + addr.to_v6().to_string() + "]";
    }
}

static inline std::string unescape_addr(const std::string& addr)
{
    std::string ret(addr);
    remove(ret.begin(), ret.end(), '[');
    remove(ret.begin(), ret.end(), ']');
    return addr;
}


static inline std::string anyaddr(const asio::ip::address& addr)
{
    if (addr.is_v4() == true)
    {
        return addr.to_v4().any().to_string();
    }
    else
    {
        return addr.to_v6().any().to_string();
    }
    gu_throw_fatal;
    throw;
}


#endif // GCOMM_ASIO_ADDR_HPP
