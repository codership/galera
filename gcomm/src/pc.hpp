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

        PC (Protonet&, const gu::URI&);

        ~PC();

        void connect();
        void connect(const gu::URI&);
        std::string get_listen_addr() const;

        void close(bool force = false);

        void handle_up(const void*, const gu::Datagram&, const ProtoUpMeta&);
        int  handle_down(gu::Datagram&, const ProtoDownMeta&);

        bool supports_uuid() const;
        const UUID& get_uuid() const;

        size_t get_mtu() const;

    private:

        GMCast*     gmcast;             // GMCast transport
        evs::Proto* evs;                // EVS protocol layer
        pc::Proto*  pc;                 // PC protocol layer
        bool        closed;             // flag for destructor
                                        // Period to wait graceful leave
        gu::datetime::Period linger;
        gu::datetime::Period announce_timeout;

        PC(const PC&);
        void operator=(const PC&);

    };

} // namespace gcomm
