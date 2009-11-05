/**
 * Generic multicast transport. Uses tcp connections if real multicast 
 * is not available.
 */
#ifndef TRANSPORT_GMCAST_HPP
#define TRANSPORT_GMCAST_HPP

#include "gcomm/common.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/types.hpp"

namespace gcomm
{

    class UUIDToAddressMap;
    
    class GMCastProto;
    class GMCastProtoMap;
    class GMCastNode;
    class GMCastMessage;
    
    class GMCast : public Transport
    {
        UUID my_uuid;
        
        /* */
        typedef GMCastProtoMap ProtoMap; // @todo: purpose of this typedef?
        ProtoMap* proto_map;
        ProtoMap* spanning_tree;
        
        /* */
        Transport*  listener;
        std::string listen_addr;
        std::string initial_addr;
        
        static const long max_retry_cnt = 1 << 24;
        
        struct Timing
        {
            UUID uuid;
            gu::datetime::Date last_seen;
            gu::datetime::Date next_reconnect;
            int  retry_cnt;
            
            Timing(const gu::datetime::Date& last_seen_,
                   const gu::datetime::Date& next_reconnect_,
                   const UUID& uuid_)
                :
                uuid           (uuid_),
                last_seen      (last_seen_),
                next_reconnect (next_reconnect_),
                retry_cnt      (0)
            {}
        };
        
        typedef std::map<const std::string, Timing > AddrList;
        
        AddrList pending_addrs;
        AddrList remote_addrs;
        
        gu::datetime::Period check_period;
        gu::datetime::Date next_check;
        
        gu::datetime::Date handle_timers();
        
        const UUID& get_uuid(const AddrList::const_iterator i) const
        {
            return i->second.uuid;
        }
        
        const std::string& get_address(const AddrList::const_iterator i) const
        {
            return i->first;
        }
        
        void set_last_seen(AddrList::iterator i, const gu::datetime::Date& t)
        {
            i->second.last_seen = t;
        }
        
        const gu::datetime::Date& get_last_seen(AddrList::iterator i) const
        {
            return i->second.last_seen;
        }
    

        void set_next_reconnect(AddrList::iterator i, const gu::datetime::Date& t)
        {
            i->second.next_reconnect = t;
        }
        
        const gu::datetime::Date& get_next_reconnect(AddrList::iterator i) const
        {
            return i->second.next_reconnect;
        }

        int get_retry_cnt(AddrList::iterator i)
        {
            return i->second.retry_cnt;
        }

        int get_retry_cnt(AddrList::iterator i) const
        {
            return i->second.retry_cnt;
        }

        void set_retry_cnt(AddrList::iterator i, const int cnt)
        {
            i->second.retry_cnt = cnt;
        }

        std::string group_name;

        /* Accept a new connection */
        void gmcast_accept();
        /* Connect to remote host */
        void gmcast_connect(const std::string&);
        /* Forget node */
        void gmcast_forget(const UUID&);
        /* Handle GMCastProto that has connected succesfully to remote host */
        void handle_connected(GMCastProto*);
        /* Handle GMCastProto that has finished handshake sequence */
        void handle_established(GMCastProto*);
        /* Handle GMCastProto that has failed */
        void handle_failed(GMCastProto*);
        /* Remote proto entry */
        void remove_proto(const int);
    
        bool is_connected(const std::string& addr, const UUID& uuid) const;
        void insert_address(const std::string& addr, const UUID& uuid, AddrList&);
        void update_addresses();
        void reconnect();

        void compute_spanning_tree(const UUIDToAddressMap&);
        void forward_message(const int cid, const gu::net::Datagram& rb, 
                             const GMCastMessage& msg);
    
        /* Start gmcast engine */
        void start();
        /* Stop gmcast engine */
        void stop();

        std::string self_string() const
        {
            return "(" + get_uuid().to_string() + ", " + listen_addr + ")";
        }

        GMCast (const GMCast&);
        GMCast& operator=(const GMCast&);

    public:
    
        std::list<std::string> get_addresses();
        /* Constructor */
        GMCast (Protonet&, const std::string&);
        // const string& listen_addr_, 
        //    const list<string>& remote_addrs_, 
        //     const list<string>& groups_);
    
        ~GMCast();

        /* Handle Protolay up event */
        void handle_up(int, const gu::net::Datagram&, const ProtoUpMeta&);
        /* Handle Protolay down event */
        int handle_down(const gu::net::Datagram&, const ProtoDownMeta&);
        
        // Transport interface
        
        bool supports_uuid() const { return true; }

        const UUID& get_uuid() const { return my_uuid; }
        
        void connect() { start(); }

        void close() { stop(); }
        
        void close(const UUID& uuid) { gmcast_forget(uuid); }
        
        void listen()
        {
            gcomm_throw_fatal << "gmcast transport listen not implemented";
        }
        
        Transport* accept()
        {
            gcomm_throw_fatal << "gmcast transport accept not implemented"; throw;
        }

        size_t get_mtu() const
        {
            return gu::net::Network::get_mtu() - UUID::serial_size();
        }
    };
}

#endif // TRANSPORT_GMCAST_HPP
