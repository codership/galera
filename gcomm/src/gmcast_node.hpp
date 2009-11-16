/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef GMCAST_NODE_HPP
#define GMCAST_NODE_HPP

#include "gcomm/types.hpp"
#include "gcomm/uuid.hpp"

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
    static const size_t ADDR_SIZE = 64;
    gcomm::String<ADDR_SIZE> addr;
public:

    Node(const std::string& addr_   = "") : addr   (addr_) { }
    
    
    const std::string& get_addr() const { return addr.to_string(); }

    size_t unserialize(const gu::byte_t* buf, 
                       const size_t buflen, const size_t offset)
    {
        size_t  off;
        uint32_t bits;
        gu_trace (off = gcomm::unserialize(buf, buflen, offset, &bits));
        gu_trace (off = addr.unserialize(buf, buflen, off));
        
        return off;
    }
    
    size_t serialize(gu::byte_t* buf, const size_t buflen, 
                     const size_t offset) const
    {
        size_t  off;
        uint32_t bits(0);
        gu_trace (off = gcomm::serialize(bits, buf, buflen, offset));
        gu_trace (off = addr.serialize(buf, buflen, off));
        
        return off;
    }
    
    static size_t serial_size() 
    { return (4 + ADDR_SIZE); }
};


inline std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const Node& n)
{
    return os;
}


#endif // GMCAST_NODE_HPP
