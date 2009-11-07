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
    }
}


class gcomm::gmcast::Node
{
    gcomm::UUID    uuid;
    static const size_t ADDR_SIZE = 64;
    gcomm::String<ADDR_SIZE> addr;
public:

    Node(const gcomm::UUID& uuid_   = UUID::nil(), 
         const std::string& addr_   = "") :
        uuid   (uuid_),
        addr   (addr_)
    {
        
    }

    
    const UUID& get_uuid() const  { return uuid; }
    const std::string& get_addr() const { return addr.to_string(); }

    size_t unserialize(const gu::byte_t* buf, 
                       const size_t buflen, const size_t offset)
    {
        size_t  off;
        uint32_t bits;
        gu_trace (off = gcomm::unserialize(buf, buflen, offset, &bits));
        gu_trace (off = uuid.unserialize(buf, buflen, off));
        gu_trace (off = addr.unserialize(buf, buflen, off));
        
        return off;
    }
    
    size_t serialize(gu::byte_t* buf, const size_t buflen, 
                     const size_t offset) const
    {
        size_t  off;
        uint32_t bits(0);
        gu_trace (off = gcomm::serialize(bits, buf, buflen, offset));
        gu_trace (off = uuid.serialize(buf, buflen, off));
        gu_trace (off = addr.serialize(buf, buflen, off));
        
        return off;
    }
    

    
    static size_t serial_size() 
    { return (4 + UUID::serial_size() + ADDR_SIZE); }
};


#endif // GMCAST_NODE_HPP
