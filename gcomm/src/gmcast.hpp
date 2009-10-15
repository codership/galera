/**
 * Generic multicast transport. Uses tcp connections if real multicast 
 * is not available.
 */
#ifndef TRANSPORT_GMCAST_HPP
#define TRANSPORT_GMCAST_HPP

#include "gcomm/common.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/pseudofd.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/types.hpp"

BEGIN_GCOMM_NAMESPACE

class UUIDToAddressMap;

class GMCastProto;
class GMCastProtoMap;
class GMCastNode;
class GMCastMessage;

class GMCast : public Transport, EventContext
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
        Time last_seen;
        Time next_reconnect;
        int  retry_cnt;

        Timing(const Time& last_seen_,
               const Time& next_reconnect_,
               const UUID& uuid_)
            :
            uuid           (uuid_),
            last_seen      (last_seen_),
            next_reconnect (next_reconnect_),
            retry_cnt      (0)
        {}
    };
    
    typedef map<const std::string, Timing > AddrList;

    AddrList pending_addrs;
    AddrList remote_addrs;

    const UUID& get_uuid(const AddrList::const_iterator i) const
    {
        return i->second.uuid;
    }

    const std::string& get_address(const AddrList::const_iterator i) const
    {
        return i->first;
    }

    void set_last_seen(AddrList::iterator i, const Time& t)
    {
        i->second.last_seen = t;
    }

    const Time& get_last_seen(AddrList::iterator i) const
    {
        return i->second.last_seen;
    }
    

    void set_next_reconnect(AddrList::iterator i, const Time& t)
    {
        i->second.next_reconnect = t;
    }

    const Time& get_next_reconnect(AddrList::iterator i) const
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
    void forward_message(const int cid, const ReadBuf* rb, 
                         const size_t offset, const GMCastMessage& msg);
    
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
    GMCast (const URI&, EventLoop*, Monitor*);
    // const string& listen_addr_, 
    //    const list<string>& remote_addrs_, 
    //     const list<string>& groups_);
    
    ~GMCast();

    /* Handle poll event */
    void handle_event(const int, const Event&);
    /* Handle Protolay up event */
    void handle_up(int, const ReadBuf*, size_t, const ProtoUpMeta&);
    /* Handle Protolay down event */
    int handle_down(WriteBuf* wb, const ProtoDownMeta&);

    // Transport interface
    
    bool supports_uuid() const
    {
        return true;
    }

    const UUID& get_uuid() const
    {
        return my_uuid;
    }

    size_t get_max_msg_size() const
    {
        return (65535 - 24 /* IPv4 */ - 60 /* TCP */);
        // no point to have message bigger than TCP packet payload
    }

    void connect()
    {
        start();
    }

    void close()
    {
        stop();
    }

    void close(const UUID& uuid)
    {
        gmcast_forget(uuid);
    }

    void listen()
    {
        gcomm_throw_fatal << "gmcast transport listen not implemented";
    }

    Transport* accept()
    {
        gcomm_throw_fatal << "gmcast transport accept not implemented"; throw;
    }
};

END_GCOMM_NAMESPACE

#endif // TRANSPORT_GMCAST_HPP
