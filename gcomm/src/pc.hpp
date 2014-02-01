/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "gcomm/transport.hpp"

namespace gcomm
{
    class GMCast;

    namespace evs
    {
        class Proto;
    }

    namespace pc
    {
        class Proto;
    }

    class PC : public Transport
    {
    public:

        PC (Protonet&, const gu::URI&);

        ~PC();

        void connect(bool start_prim = false);
        void connect(const gu::URI&);
        std::string listen_addr() const;

        void close(bool force = false);

        void handle_up(const void*, const Datagram&, const ProtoUpMeta&);
        int  handle_down(Datagram&, const ProtoDownMeta&);

        const UUID& uuid() const;

        size_t mtu() const;

    private:

        GMCast*     gmcast_;             // GMCast transport
        evs::Proto* evs_;                // EVS protocol layer
        pc::Proto*  pc_;                 // PC protocol layer
        bool        closed_;             // flag for destructor
                                        // Period to wait graceful leave
        gu::datetime::Period linger_;
        gu::datetime::Period announce_timeout_;

        PC(const PC&);
        void operator=(const PC&);

    };

} // namespace gcomm
