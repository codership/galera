/**
 * Generic multicast transport. Uses tcp connections if real multicast 
 * is not available.
 */
#ifndef TRANSPORT_GMCAST_HPP
#define TRANSPORT_GMCAST_HPP


#include "gcomm/common.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/uri.hpp"
#include "gcomm/string.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/pseudofd.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/types.hpp"


BEGIN_GCOMM_NAMESPACE


class GMCastProto;

class UUIDToAddressMap;


class GMCastProtoMap;
class GMCastNode;
class GMCastMessage;

class GMCast : public Transport, EventContext
{
    UUID my_uuid;

    /* */
    typedef GMCastProtoMap ProtoMap;
    ProtoMap* proto_map;
    ProtoMap* spanning_tree;

    /* */
    Transport* listener;
    string listen_addr;

    static const int max_retry_cnt = 30;

    struct Timing
    {
        UUID uuid;
        Time last_seen;
        Time next_reconnect;
        int retry_cnt;
        Timing(const Time& last_seen_, const Time& next_reconnect_,
            const UUID& uuid_) :
            uuid(uuid_),
            last_seen(last_seen_),
            next_reconnect(next_reconnect_),
            retry_cnt(0)
        {
        }
    };
    
    std::string initial_addr;

    typedef map<const string, Timing > AddrList;

    AddrList pending_addrs;
    AddrList remote_addrs;

    const UUID& get_uuid(const AddrList::const_iterator i) const
    {
        return i->second.uuid;
    }

    const string& get_address(const AddrList::const_iterator i) const
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


    string group_name;

    /* Accept a new connection */
    void gmcast_accept();
    /* Connect to remote host */
    void gmcast_connect(const string&);
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
    void insert_address(const string& addr, const UUID& uuid, AddrList&);
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
        return "(" + get_uuid().to_string() + "," + listen_addr + ")";
    }


    GMCast(const GMCast&);
    void operator=(GMCast&);

public:
    
    list<string> get_addresses();
    /* Constructor */
    GMCast(const URI&, EventLoop*, Monitor*);
    // const string& listen_addr_, 
    //    const list<string>& remote_addrs_, 
    //     const list<string>& groups_);
    
    ~GMCast();

    
    /* Handle poll event */
    void handle_event(const int, const Event&);
    /* Handle Protolay up event */
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    /* Handle Protolay down event */
    int handle_down(WriteBuf* wb, const ProtoDownMeta*);

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
        return 1024*1024;
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
        throw FatalException("gmcast transport listen not implemented");
    }

    Transport* accept()
    {
        throw FatalException("gmcast transport accept not implemented");
    }
    

};


END_GCOMM_NAMESPACE

#endif // TRANSPORT_GMCAST_HPP
