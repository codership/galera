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
    namespace pc
    {
        class Proto;
    }
    
    class PC : public Transport
    {
    public:
        
        PC (Protonet&, const std::string&);
        
        ~PC();
        
        void connect();
        void close();
        
        void handle_up(const void*, const gu::net::Datagram&, const ProtoUpMeta&);
        int  handle_down(gu::net::Datagram&, const ProtoDownMeta&);

        bool supports_uuid() const;
        const UUID& get_uuid() const;
        
        size_t get_mtu() const;

    private:

        GMCast*     gmcast;             // GMCast transport
        evs::Proto* evs;                // EVS protocol layer
        pc::Proto*  pc;                 // PC protocol layer
        bool        closed;             // flag for destructor
                                        // Period to wait graceful leave
        gu::datetime::Period leave_grace_period;

        PC(const PC&);
        void operator=(const PC&);
        
    };
    
} // namespace gcomm
