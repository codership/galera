/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gcomm/transport.hpp"

namespace gcomm
{
    class GMCast;
    namespace evs
    {
        class Proto;
    } // namespace gcomm
    class PCProto;
    
    
    class PC : public Transport
    {
        GMCast*     gmcast;                           // GMCast transport
        evs::Proto* evs;                // EVS protocol layer
        PCProto*    pc;                 // PC protocol layer
        gu::datetime::Period leave_grace_period; // Period to wait graceful leave
        
        PC(const PC&);
        void operator=(const PC&);
        
    public:
        
        PC (Protonet&, const std::string&);
        
        ~PC();
        
        void connect();
        void close();
        
        void handle_up(int, const gu::net::Datagram&, const ProtoUpMeta&);
        int  handle_down(const gu::net::Datagram&, const ProtoDownMeta&);

        bool supports_uuid() const;
        const UUID& get_uuid() const;
        
        size_t get_mtu() const;
    };
    
} // namespace gcomm
