/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

#ifndef GMCAST_NODE_HPP
#define GMCAST_NODE_HPP

#include "gcomm/types.hpp"
#include "gcomm/uuid.hpp"

#include "gu_serialize.hpp"

namespace gcomm
{
    namespace gmcast
    {
        class Node;
        std::ostream& operator<<(std::ostream&, const Node&);
    }
}


class gcomm::gmcast::Node
{

public:

    Node(const std::string& addr   = "") : addr_(addr), mcast_addr_("") { }

    const std::string& addr() const { return addr_.to_string(); }
    const std::string& mcast_addr() const { return mcast_addr_.to_string(); }

    size_t unserialize(const gu::byte_t* buf,
                       const size_t buflen, const size_t offset)
    {
        size_t  off;
        uint32_t bits;
        gu_trace (off = gu::unserialize4(buf, buflen, offset, bits));
        gu_trace (off = addr_.unserialize(buf, buflen, off));
        gu_trace (off = mcast_addr_.unserialize(buf, buflen, off));
        return off;
    }

    size_t serialize(gu::byte_t* buf, const size_t buflen,
                     const size_t offset) const
    {
        size_t  off;
        uint32_t bits(0);
        gu_trace (off = gu::serialize4(bits, buf, buflen, offset));
        gu_trace (off = addr_.serialize(buf, buflen, off));
        gu_trace (off = mcast_addr_.serialize(buf, buflen, off));
        return off;
    }

    static size_t serial_size() { return (4 + 2 * ADDR_SIZE); }

private:
    static const size_t ADDR_SIZE = 64;
    gcomm::String<ADDR_SIZE> addr_;
    gcomm::String<ADDR_SIZE> mcast_addr_;
};


inline std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const Node& n)
{
    return os;
}


#endif // GMCAST_NODE_HPP
