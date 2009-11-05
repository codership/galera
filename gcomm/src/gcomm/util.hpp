#ifndef _GCOMM_UTIL_HPP_
#define _GCOMM_UTIL_HPP_

#include <sstream>
#include <cstring>

#include "gcomm/exception.hpp"
#include "gcomm/types.hpp"
#include "gcomm/protostack.hpp"



namespace gcomm
{

    // @todo: wrong function - port is not mandatory by RFC, will also fail for IPv6
    inline std::string __parse_host(const std::string& str)
    {
        size_t sep = str.find(':');
        if (sep == std::string::npos)
        {
            gcomm_throw_runtime (EINVAL) << "Invalid auth str";
        }
        return str.substr(0, sep);
    }
    
    // @todo: wrong function - port is not mandatory by RFC, will also fail for IPv6
    inline std::string __parse_port(const std::string& str)
    {
        size_t sep = str.find(':');
        if (sep == std::string::npos)
        {
            gcomm_throw_runtime (EINVAL) << "Invalid auth str";
        }
        return str.substr(sep + 1);
    }
    
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
    
    // Conveinence function to iterate over protostacks
    void event_loop(gu::net::Network& el, 
                    std::vector<gcomm::Protostack>& protos, 
                    const std::string tstr = "PT1S");
    
} // namespace gcomm

#endif // _GCOMM_UTIL_HPP_
