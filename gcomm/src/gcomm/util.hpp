/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#ifndef _GCOMM_UTIL_HPP_
#define _GCOMM_UTIL_HPP_

#include "gu_network.hpp"

namespace gcomm
{
    inline bool host_is_any (const std::string& host)
    {   
        return (host.length() == 0 || host == "0.0.0.0" ||
                host.find ("::/128") <= 1);
    }
    
    
    template <class C> 
    size_t serialize(const C& c, gu::Buffer& buf)
    {
        const size_t prev_size(buf.size());
        buf.resize(buf.size() + c.serial_size());
        size_t ret;
        gu_trace(ret = c.serialize(&buf[0] + prev_size, buf.size(), 
                                   prev_size));
        assert(ret == prev_size + c.serial_size());
        return ret;
    }
    
    template <class C>
    size_t unserialize(const gu::Buffer& buf, size_t offset, C& c)
    {
        size_t ret;
        gu_trace(ret = c.unserialize(buf, buf.size(), offset));
        return ret;
    }
    
    template <class M>
    void push_header(const M& msg, gu::net::Datagram& dg)
    {
        dg.get_header().resize(dg.get_header().size() + msg.serial_size());
        memmove(&dg.get_header()[0] + msg.serial_size(),
                &dg.get_header()[0], 
                dg.get_header().size() - msg.serial_size());
        msg.serialize(&dg.get_header()[0], 
                      dg.get_header().size(), 0);
    }
    
    template <class M>
    void pop_header(const M& msg, gu::net::Datagram& dg)
    {
        memmove(&dg.get_header()[0],
                &dg.get_header()[0] + msg.serial_size(),
                dg.get_header().size() - msg.serial_size());
        dg.get_header().resize(dg.get_header().size() - msg.serial_size());
    }
} // namespace gcomm

#endif // _GCOMM_UTIL_HPP_
