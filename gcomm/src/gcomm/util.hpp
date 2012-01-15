/*
 * Copyright (C) 2012 Codership Oy <info@codership.com>
 */

#ifndef _GCOMM_UTIL_HPP_
#define _GCOMM_UTIL_HPP_

#include "gu_network.hpp"
#include "gu_logger.hpp"
#include "gu_throw.hpp"

#include <algorithm>

namespace gcomm
{
    inline std::string
    uri_string (const std::string& scheme, const std::string& addr,
                const std::string& port = std::string(""))
    {
        if (port.length() > 0)
            return (scheme + "://" + addr + ':' + port);
        else
            return (scheme + "://" + addr);
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
    void push_header(const M& msg, gu::Datagram& dg)
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
            gu_throw_fatal;
        }
        msg.serialize(dg.get_header(),
                      dg.get_header_size(),
                      dg.get_header_offset() - msg.serial_size());
        dg.set_header_offset(dg.get_header_offset() - msg.serial_size());
#endif
    }


    template <class M>
    void pop_header(const M& msg, gu::Datagram& dg)
    {
#if 0
        memmove(&dg.get_header()[0],
                &dg.get_header()[0] + msg.serial_size(),
                dg.get_header().size() - msg.serial_size());
        dg.get_header().resize(dg.get_header().size() - msg.serial_size());
#else
        assert(dg.get_header_size() >= dg.get_header_offset() + msg.serial_size());
        dg.set_header_offset(dg.get_header_offset() + msg.serial_size());
#endif
    }


    inline const gu::byte_t* get_begin(const gu::Datagram& dg)
    {
        return (dg.get_offset() < dg.get_header_len() ?
                dg.get_header() + dg.get_header_offset() + dg.get_offset() :
                &dg.get_payload()[0] + (dg.get_offset() - dg.get_header_len()));
    }
    inline size_t get_available(const gu::Datagram& dg)
    {
        return (dg.get_offset() < dg.get_header_len() ?
                dg.get_header_len() - dg.get_offset() :
                dg.get_payload().size() - (dg.get_offset() - dg.get_header_len()));
    }


    template <class M>
    class Critical
    {
    public:
        Critical(M& monitor) : monitor_(monitor)
        { monitor_.enter(); }

        ~Critical() { monitor_.leave(); }
    private:
        M& monitor_;
    };

} // namespace gcomm

#endif // _GCOMM_UTIL_HPP_
