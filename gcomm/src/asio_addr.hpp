/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 */

#ifndef GCOMM_ASIO_ADDR_HPP
#define GCOMM_ASIO_ADDR_HPP

#include "gu_exception.hpp"

#include "asio_protonet.hpp"

#include <string>
#include <algorithm>

namespace gcomm
{
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
        size_t pos(ret.find('['));
        if (pos != std::string::npos) ret.erase(pos, 1);
        pos = ret.find(']');
        if (pos != std::string::npos) ret.erase(pos, 1);
        return ret;
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
    }
}

template <class S>
void set_fd_options(S& socket)
{
    long flags(FD_CLOEXEC);
    if (fcntl(socket.native(), F_SETFD, flags) == -1)
    {
        gu_throw_error(errno) << "failed to set FD_CLOEXEC";
    }
}


#endif // GCOMM_ASIO_ADDR_HPP
