/*
 * Copyright (C) 2009-2020 Codership Oy <info@codership.com>
 */

/*
 * Generic multicast transport. Uses tcp connections if real multicast
 * is not available.
 */
#ifndef GCOMM_GMCAST_HPP
#define GCOMM_GMCAST_HPP

#include "gmcast_proto.hpp"

#include "gcomm/uuid.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/types.hpp"


#include <set>

#ifndef GCOMM_GMCAST_MAX_VERSION
#define GCOMM_GMCAST_MAX_VERSION 0
#endif // GCOMM_GMCAST_MAX_VERSION

namespace gcomm
{
    namespace gmcast
    {
        class Proto;
        class Node;
        class Message;
    }

    class GMCast : public Transport
    {
    public:

        GMCast (Protonet&, const gu::URI&, const UUID* my_uuid = NULL);
        ~GMCast();

        // Protolay interface
        void handle_up(const void*, const Datagram&, const ProtoUpMeta&);
        int  handle_down(Datagram&, const ProtoDownMeta&);
        void handle_stable_view(const View& view);
        void handle_evict(const UUID& uuid);
        std::string handle_get_address(const UUID& uuid) const;
        bool set_param(const std::string& key, const std::string& val,
                       Protolay::sync_param_cb_t& sync_param_cb);
        // Transport interface
        const UUID& uuid() const { return my_uuid_; }
        SegmentId segment() const { return segment_; }
        void connect_precheck(bool start_prim);
        void connect();
        void connect(const gu::URI&);
        void close(bool force = false);
        void close(const UUID& uuid)
        {
            gmcast_forget(uuid, time_wait_);
        }

        void listen()
        {
            gu_throw_fatal << "gmcast transport listen not implemented";
        }

        // Configured listen address
        std::string configured_listen_addr() const
        {
            return listen_addr_;
        }

        // Listen adddress obtained from listening socket.
        std::string listen_addr() const
        {
            if (listener_ == 0)
            {
                gu_throw_error(ENOTCONN) << "not connected";
            }
            return listener_->listen_addr();
        }

        Transport* accept()
        {
            gu_throw_fatal << "gmcast transport accept not implemented";
        }

        size_t mtu() const
        {
            return pnet_.mtu() - (4 + UUID::serial_size());
        }

        void remove_viewstate_file() const
        {
            ViewState::remove_file(conf_);
        }

    private:

        GMCast (const GMCast&);
        GMCast& operator=(const GMCast&);

        static const long max_retry_cnt_;

        class AddrEntry
        {
        public:

            AddrEntry(const gu::datetime::Date& last_seen,
                      const gu::datetime::Date& next_reconnect,
                      const UUID&               uuid)
                :
                uuid_           (uuid),
                last_seen_      (last_seen),
                next_reconnect_ (next_reconnect),
                last_connect_   (0),
                retry_cnt_      (0),
                max_retries_    (0)
            { }

            AddrEntry(const AddrEntry& other)
                :
                uuid_(other.uuid_),
                last_seen_(other.last_seen_),
                next_reconnect_(other.next_reconnect_),
                last_connect_(other.last_connect_),
                retry_cnt_(other.retry_cnt_),
                max_retries_(other.max_retries_)
            { }

            const UUID& uuid() const { return uuid_; }

            void set_last_seen(const gu::datetime::Date& d) { last_seen_ = d; }

            const gu::datetime::Date& last_seen() const
            { return last_seen_; }

            void set_next_reconnect(const gu::datetime::Date& d)
            { next_reconnect_ = d; }

            const gu::datetime::Date& next_reconnect() const
            { return next_reconnect_; }

            void set_last_connect()
            {
                last_connect_ = gu::datetime::Date::monotonic();
            }

            const gu::datetime::Date& last_connect() const
            {
                return last_connect_;
            }

            void set_retry_cnt(const int r) { retry_cnt_ = r; }

            int retry_cnt() const { return retry_cnt_; }

            void set_max_retries(int mr) { max_retries_ = mr; }
            int max_retries() const { return max_retries_; }

        private:
            friend std::ostream& operator<<(std::ostream&, const AddrEntry&);
            void operator=(const AddrEntry&);
            UUID uuid_;
            gu::datetime::Date last_seen_;
            gu::datetime::Date next_reconnect_;
            gu::datetime::Date last_connect_;
            int  retry_cnt_;
            int  max_retries_;
        };



        typedef Map<std::string, AddrEntry> AddrList;
        class AddrListUUIDCmp
        {
        public:
            AddrListUUIDCmp(const UUID& uuid) : uuid_(uuid) { }
            bool operator()(const AddrList::value_type& cmp) const
            {
                return (cmp.second.uuid() == uuid_);
            }
        private:
            UUID uuid_;
        };

        int               version_;
        static const int  max_version_ = GCOMM_GMCAST_MAX_VERSION;
        uint8_t           segment_;
        UUID              my_uuid_;
        bool              use_ssl_;
        std::string       group_name_;
        std::string       listen_addr_;
        std::set<std::string> initial_addrs_;
        std::string       mcast_addr_;
        std::string       bind_ip_;
        int               mcast_ttl_;
        std::shared_ptr<Acceptor> listener_;
        SocketPtr         mcast_;
        AddrList          pending_addrs_;
        AddrList          remote_addrs_;
        AddrList          addr_blacklist_;
        bool              relaying_;
        int               isolate_;
        bool              prim_view_reached_;

        gmcast::ProtoMap*  proto_map_;
        struct RelayEntry
        {
            gmcast::Proto* proto;
            gcomm::Socket* socket;
            RelayEntry(gmcast::Proto* p, gcomm::Socket* s)
                : proto(p), socket(s) { }
            bool operator<(const RelayEntry& other) const
            {
                return (socket < other.socket);
            }
        };
        void send(const RelayEntry&, int segment, gcomm::Datagram& dg);
        typedef std::set<RelayEntry> RelaySet;
        RelaySet relay_set_;

        typedef std::vector<RelayEntry> Segment;
        typedef std::map<uint8_t, Segment> SegmentMap;
        SegmentMap segment_map_;
        // self index in local segment when ordered by UUID
        size_t self_index_;
        gu::datetime::Period time_wait_;
        gu::datetime::Period check_period_;
        gu::datetime::Period peer_timeout_;
        int                  max_initial_reconnect_attempts_;
        gu::datetime::Date next_check_;
        gu::datetime::Date handle_timers();

        // Grant Proto access to private helper methods.
        friend class gcomm::gmcast::Proto;

        /*
         * Checks if the proto is a remote connection point for
         * locally originated connection. The proto
         * is required to have gone through initial handshake
         * sequence so that the remote endpoint UUID is known.
         *
         * @param proto Protocol entry
         *
         * @return True if matching entry was found and blacklisted,
         *         false otherwise.
         */
        bool is_own(const gmcast::Proto *proto) const;

        /*
         * Add a proto entry to blacklist. After calling this reconnect
         * attempts to remote endpoint corresponding to proto are
         * disabled.
         *
         * @param proto Proto entry to be blacklisted.
         */
        void blacklist(const gmcast::Proto* proto);

        /*
         * Check if the proto entry is not originated from own
         * connection and there already is a proto entry with
         * the same remote UUID but with different address.
         *
         * It is required that the proto has received handshake
         * message from remote endpoint so that the remote
         * endpoint identity is known.
         *
         */
        bool is_not_own_and_duplicate_exists(const gmcast::Proto* proto) const;

        /**
         * Return boolean denoting if the primary view has been reached.
         */
        bool prim_view_reached() const { return prim_view_reached_; }

        // Erase ProtoMap entry in a safe way so that all lookup lists
        // become properly updated.
        void erase_proto(gmcast::ProtoMap::iterator);
        // Accept new connection
        void gmcast_accept();
        // Initialize connecting to remote host
        void gmcast_connect(const std::string&);
        // Forget node
        void gmcast_forget(const gcomm::UUID&, const gu::datetime::Period&);
        // Handle proto entry that has established connection to remote host
        void handle_connected(gmcast::Proto*);
        // Handle proto entry that has successfully finished handshake
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
        //
        void check_liveness();
        void relay(const gmcast::Message& msg, const Datagram& dg,
                   const void* exclude_id);
        // Reconnecting
        void reconnect();

        void set_initial_addr(const gu::URI&);
        void add_or_del_addr(const std::string&);

        std::string self_string() const
        {
            std::ostringstream os;
            os << '(' << my_uuid_ << ", '" << listen_addr_ << "')";
            return os.str();
        }

        friend std::ostream& operator<<(std::ostream&, const AddrEntry&);
    };

    inline std::ostream& operator<<(std::ostream& os, const GMCast::AddrEntry& ae)
    {
        return (os << ae.uuid_
                << " last_seen=" << ae.last_seen_
                << " next_reconnect=" << ae.next_reconnect_
                << " retry_cnt=" << ae.retry_cnt_);
    }

}

#endif // GCOMM_GMCAST_HPP
