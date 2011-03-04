/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*
 * Generic multicast transport. Uses tcp connections if real multicast
 * is not available.
 */
#ifndef TRANSPORT_GMCAST_HPP
#define TRANSPORT_GMCAST_HPP

#include "gcomm/uuid.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/types.hpp"

#ifndef GCOMM_GMCAST_MAX_VERSION
#define GCOMM_GMCAST_MAX_VERSION 0
#endif // GCOMM_GMCAST_MAX_VERSION

namespace gcomm
{
    namespace gmcast
    {
        class Proto;
        class ProtoMap;
        class Node;
        class Message;
    }

    class GMCast : public Transport
    {
    public:

        GMCast (Protonet&, const gu::URI&);
        ~GMCast();

        // Protolay interface
        void handle_up(const void*, const gu::Datagram&, const ProtoUpMeta&);
        int  handle_down(gu::Datagram&, const ProtoDownMeta&);
        void handle_stable_view(const View& view);
        // Transport interface
        bool supports_uuid()   const { return true; }
        const UUID& get_uuid() const { return my_uuid; }
        void connect();
        void close();
        void close(const UUID& uuid) { gmcast_forget(uuid); }

        void listen()
        {
            gu_throw_fatal << "gmcast transport listen not implemented";
        }

        Transport* accept()
        {
            gu_throw_fatal << "gmcast transport accept not implemented"; throw;
        }

        size_t get_mtu() const
        {
            return gu::net::Network::get_mtu() - (4 + UUID::serial_size());
        }

    private:

        GMCast (const GMCast&);
        GMCast& operator=(const GMCast&);

        static const long max_retry_cnt = 12*3600;

        class AddrEntry
        {
        public:

            AddrEntry(const gu::datetime::Date& last_seen_,
                      const gu::datetime::Date& next_reconnect_,
                      const UUID& uuid_) :
                uuid           (uuid_),
                last_seen      (last_seen_),
                next_reconnect (next_reconnect_),
                retry_cnt      (0)
            { }

            const UUID& get_uuid() const { return uuid; }

            void set_last_seen(const gu::datetime::Date& d) { last_seen = d; }

            const gu::datetime::Date& get_last_seen() const
            { return last_seen; }

            void set_next_reconnect(const gu::datetime::Date& d)
            { next_reconnect = d; }

            const gu::datetime::Date& get_next_reconnect() const
            { return next_reconnect; }

            void set_retry_cnt(const int r) { retry_cnt = r; }

            int get_retry_cnt() const { return retry_cnt; }

        private:
            friend std::ostream& operator<<(std::ostream&, const AddrEntry&);
            void operator=(const AddrEntry&);
            UUID uuid;
            gu::datetime::Date last_seen;
            gu::datetime::Date next_reconnect;
            int  retry_cnt;
        };



        typedef Map<std::string, AddrEntry> AddrList;
        class AddrListUUIDCmp
        {
        public:
            AddrListUUIDCmp(const UUID& uuid) : uuid_(uuid) { }
            bool operator()(const AddrList::value_type& cmp) const
            {
                return (cmp.second.get_uuid() == uuid_);
            }
        private:
            UUID uuid_;
        };

        int               version;
        static const int max_version_ = GCOMM_GMCAST_MAX_VERSION;
        UUID              my_uuid;
        std::string       group_name;
        std::string       listen_addr;
        std::string       initial_addr;
        std::string       mcast_addr;
        std::string       bind_ip;
        int               mcast_ttl;
        Acceptor*         listener;
        SocketPtr         mcast;
        AddrList          pending_addrs;
        AddrList          remote_addrs;

        gmcast::ProtoMap*  proto_map;
        std::list<Socket*> mcast_tree;

        gu::datetime::Period time_wait;
        gu::datetime::Period check_period;
        gu::datetime::Date next_check;
        gu::datetime::Date handle_timers();

        // Accept new connection
        void gmcast_accept();
        // Initialize connecting to remote host
        void gmcast_connect(const std::string&);
        // Forget node
        void gmcast_forget(const gcomm::UUID&);
        // Handle proto entry that has established connection to remote host
        void handle_connected(gmcast::Proto*);
        // Handle proto entry that has succesfully finished handshake
        // sequence
        void handle_established(gmcast::Proto*);
        // Handle proto entry that has failed
        void handle_failed(gmcast::Proto*);

        // Check if there exists connection that matches to either
        // remote addr or uuid
        bool is_connected(const std::string& addr, const UUID& uuid) const;
        // Inset address to address list
        void insert_address(const std::string& addr, const UUID& uuid, AddrList&);
        // Scan through proto entries and update address lists
        void update_addresses();
        // Reconnecting
        void reconnect();

        std::string self_string() const
        {
            std::ostringstream os;
            os << '(' << my_uuid << ", '" << listen_addr << "')";
            return os.str();
        }

        friend std::ostream& operator<<(std::ostream&, const AddrEntry&);
    };

    inline std::ostream& operator<<(std::ostream& os, const GMCast::AddrEntry& ae)
    {
        return (os << ae.uuid
                << " last_seen=" << ae.last_seen
                << " next_reconnect=" << ae.next_reconnect
                << " retry_cnt=" << ae.retry_cnt);
    }

}

#endif // TRANSPORT_GMCAST_HPP
