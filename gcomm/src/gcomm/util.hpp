/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#ifndef _GCOMM_UTIL_HPP_
#define _GCOMM_UTIL_HPP_

#include "gu_network.hpp"
#include "gu_logger.hpp"

#include <algorithm>

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
#if 0
        dg.get_header().resize(dg.get_header().size() + msg.serial_size());
        memmove(&dg.get_header()[0] + msg.serial_size(),
                &dg.get_header()[0], 
                dg.get_header().size() - msg.serial_size());
        msg.serialize(&dg.get_header()[0], 
                      dg.get_header().size(), 0);
#else
        if (dg.get_header_offset() < msg.serial_size())
        {
            const size_t prev_size(dg.get_header().size());
            dg.get_header().resize((dg.get_header().size() - dg.get_header_offset()) + msg.serial_size());
            std::copy_backward(
                dg.get_header().begin() + dg.get_header_offset(),
                dg.get_header().begin() + prev_size,
                dg.get_header().end());
            dg.set_header_offset(dg.get_header_offset() 
                                 + (dg.get_header().size() - prev_size));
        }
        assert(dg.get_header_offset() >= msg.serial_size());
        msg.serialize(&dg.get_header()[0], 
                      dg.get_header().size(),
                      dg.get_header_offset() - msg.serial_size());
        dg.set_header_offset(dg.get_header_offset() - msg.serial_size());
#endif
    }
    
    template <class M>
    void pop_header(const M& msg, gu::net::Datagram& dg)
    {
#if 0
        memmove(&dg.get_header()[0],
                &dg.get_header()[0] + msg.serial_size(),
                dg.get_header().size() - msg.serial_size());
        dg.get_header().resize(dg.get_header().size() - msg.serial_size());
#else
        assert(dg.get_header().size() >= dg.get_header_offset() + msg.serial_size());
        dg.set_header_offset(dg.get_header_offset() + msg.serial_size());
#endif
    }
} // namespace gcomm

#endif // _GCOMM_UTIL_HPP_
