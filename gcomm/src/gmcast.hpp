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

#include <map>
#include <list>
#include <cstdlib>

using std::map;
using std::multimap;
using std::list;
using std::string;


BEGIN_GCOMM_NAMESPACE

class GMCastNode
{
    bool operational;
    uint8_t weight;
    UUID uuid;
    static const size_t ADDR_SIZE = 64;
    char address[ADDR_SIZE];
public:
    GMCastNode() : operational(false), weight(0xff), uuid() {
        memset(address, 0, sizeof(address));
    }
    
    GMCastNode(const bool operational_, const UUID& uuid_, 
               const string& address_) :
        operational(operational_), 
        uuid(uuid_) {
        if (address_.size() > sizeof(address) - 1)
            throw FatalException("");
        memset(address, 0, sizeof(address));
        strcpy(address, address_.c_str());
    }
    
    void set_operational(bool op) {
        operational = op;
    }
    
    bool is_operational() const {
        return operational;
    }
    
    const UUID& get_uuid() const {
        return uuid;
    }
    
    size_t read(const void* buf, const size_t buflen, const size_t offset) {
        size_t off;
        uint8_t byte;
        if ((off = gcomm::read(buf, buflen, offset, &byte)) == 0)
            return 0;
        operational = byte & 0x1;
        if ((off = uuid.read(buf, buflen, off)) == 0)
            return 0;
        if (off + ADDR_SIZE > buflen)
            return 0;
        memcpy(address, (char*)buf + off, ADDR_SIZE);
        size_t i;
        for (i = 0; i < ADDR_SIZE; ++i) {
            if (address[i] == '\0')
                break;
        }
        if (i == ADDR_SIZE) {
            LOG_WARN("address was not '\0' terminated");
            return 0;
        }
        off += ADDR_SIZE;
        return off;
    }

    size_t write(void* buf, const size_t buflen, const size_t offset) const {
        size_t off;
        uint8_t byte = operational ? 0x1 : 0;
        if ((off = gcomm::write(byte, buf, buflen, offset)) == 0)
            return 0;
        if ((off = uuid.write(buf, buflen, off)) == 0)
            return 0;
        if (off + ADDR_SIZE > buflen)
            return 0;
        memcpy((char*)buf + off, address, ADDR_SIZE);
        off += ADDR_SIZE;
        return off;
    }
    
    string get_address() const {
        return string(address);
    }
    
    static size_t size() {
        return 1 + UUID::size() + ADDR_SIZE;
    }
};

class GMCastMessage
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t ttl;
    UUID source_uuid;
    string node_address;
    string group_name;
public:
    typedef std::list<GMCastNode> NodeList;
private:
    NodeList* node_list;
public:
    
    enum Flags {
        F_GROUP_NAME = 1 << 0,
        F_NODE_NAME = 1 << 1,
        F_NODE_ADDRESS = 1 << 2,
        F_NODE_LIST = 1 << 3
    };
    
    enum PacketType 
    {
        P_INVALID = 0,
        P_HANDSHAKE = 1,
        P_HANDSHAKE_RESPONSE = 2,
        P_HANDSHAKE_OK = 3,
        P_HANDSHAKE_FAIL = 4,
        P_TOPOLOGY_CHANGE = 5,
        /* Leave room for future use */
        P_USER_BASE = 8
    };
    
    /* Default ctor */
    GMCastMessage() :
        version(0), 
        type(0), 
        flags(0), 
        ttl(0), 
        source_uuid(), 
        group_name(),
        node_list(0)
    {
    }
    
    /* Ctor for handshake, handshake ok and handshake fail */
    GMCastMessage(const uint8_t type_, const UUID& source_uuid_) :
        version(0), 
        type(type_), 
        flags(0), 
        ttl(1), 
        source_uuid(source_uuid_), 
        group_name(),
        node_list(0)
    {
        if (type != P_HANDSHAKE && type != P_HANDSHAKE_OK && 
            type != P_HANDSHAKE_FAIL)
            throw FatalException("invalid message type");
        
    }
    
    /* Ctor for user message */
    GMCastMessage(const uint8_t type_, const UUID& source_uuid_, 
                 const uint8_t ttl_) :
        version(0), 
        type(type_), 
        flags(0), 
        ttl(ttl_), 
        source_uuid(source_uuid_), 
        group_name(""),
        node_list(0)
    {
        if (type < P_USER_BASE)
            throw FatalException("invalid message type");
    }
    
    /* Ctor for handshake response */
    GMCastMessage(const uint8_t type_, const UUID& source_uuid_,
                 const string& node_address_,
                 const char* group_name_) : 
        version(0), 
        type(type_), 
        flags(F_GROUP_NAME | F_NODE_ADDRESS), 
        ttl(1),
        source_uuid(source_uuid_),
        node_address(node_address_),
        group_name(group_name_),
        node_list(0)

    {
        if (type != P_HANDSHAKE_RESPONSE)
            throw FatalException("invalid message type");
    }

    /* Ctor for topology change */
    GMCastMessage(const uint8_t type_, 
                 const UUID& source_uuid_,
                 const string& group_name_,
                 const NodeList& nodes) :
        version(0),
        type(type_),
        flags(F_GROUP_NAME | F_NODE_LIST), 
        ttl(1),
        source_uuid(source_uuid_),
        group_name(group_name_),
        node_list(new NodeList(nodes))
    {
        if (type != P_TOPOLOGY_CHANGE)
            throw FatalException("invalid message type");
    }
    
    ~GMCastMessage() 
    {
        delete node_list;
    }

    
    size_t write(void* buf, const size_t buflen, const size_t offset) const {
        size_t off;
        /* Version */
        if ((off = gcomm::write(version, buf, buflen, offset)) == 0)
            return 0;
        /* Type */
        if ((off = gcomm::write(type, buf, buflen, off)) == 0)
            return 0;
        /* Flags  */
        if ((off = gcomm::write(flags, buf, buflen, off)) == 0)
            return 0;
        /* TTL */
        if ((off = gcomm::write(ttl, buf, buflen, off)) == 0)
            return 0;
        if ((off = source_uuid.write(buf, buflen, off)) == 0)
            return 0;

        if (flags & F_NODE_ADDRESS)
        {
            if ((off = write_string(node_address.c_str(), buf, buflen, off)) == 0)
                return 0;
        }
        if (flags & F_GROUP_NAME) 
        {
            if ((off = write_string(group_name.c_str(), buf, buflen, off)) == 0)
                return 0;
        }
        
        if (flags & F_NODE_LIST) 
        {
            if ((off = gcomm::write(static_cast<uint16_t>(node_list->size()), 
                                    buf, buflen, off)) == 0)
                return 0;
            for (NodeList::const_iterator i = node_list->begin();
                 i != node_list->end(); ++i) 
            {
                if ((off = i->write(buf, buflen, off)) == 0)
                    return 0;
            }
        }
        return off;
    }
    
    size_t read_v0(const void* buf, const size_t buflen, const size_t offset) {
        size_t off;
        if ((off = gcomm::read(buf, buflen, offset, &type)) == 0)
            return 0;
        if ((off = gcomm::read(buf, buflen, off, &flags)) == 0)
            return 0;
        if ((off = gcomm::read(buf, buflen, off, &ttl)) == 0)
            return 0;
        if ((off = source_uuid.read(buf, buflen, off)) == 0)
            return 0;

        if (flags & F_NODE_ADDRESS)
        {
            char* addr = 0;
            if ((off = read_string(buf, buflen, off, &addr)) == 0)
                return 0;
            node_address = addr;
            free(addr);
        }
        
        if (flags & F_GROUP_NAME) 
        {
            char* grp = 0;
            if ((off = read_string(buf, buflen, off, &grp)) == 0)
                return 0;
            group_name = grp;
            free(grp);
        }
        
        if (flags & F_NODE_LIST)
        {
            node_list = new NodeList();
            uint16_t size;
            if ((off = gcomm::read(buf, buflen, off, &size)) == 0)
                return 0;
            for (uint16_t i = 0; i < size; ++i) 
            {
                GMCastNode node;
                if ((off = node.read(buf, buflen, off)) == 0)
                    return 0;
                node_list->push_back(node);
            }
        }
        LOG_DEBUG(UInt8(type).to_string() + "," 
                  + UInt8(flags).to_string() + "," 
                  + UInt8(ttl).to_string() + ","
                  + (F_GROUP_NAME ? group_name : ""));
        return off;
    }
    
    size_t read(const void* buf, const size_t buflen, const size_t offset) {
        size_t off;
        
        if ((off = gcomm::read(buf, buflen, offset, &version)) == 0)
            return 0;
        switch (version) {
        case 0:
            return read_v0(buf, buflen, off);
        default:
            return 0;
        }
    }
    
    size_t size() const {
        return 4                 /* Common header */ 
            + source_uuid.size() /* Source uuid */
            /* GMCast address if set */
            + (flags & F_NODE_ADDRESS ? node_address.size() + 1 : 0)
            /* Group name if set */
            + (flags & F_GROUP_NAME ? group_name.size() + 1 : 0)
            /* Node list if set */
            + (flags & F_NODE_LIST ? 
               2 + node_list->size()*GMCastNode::size() : 0);
    }
    
    uint8_t get_version() const {
        return version;
    }
    
    uint8_t get_type() const {
        return type;
    }

    uint8_t get_ttl() const {
        return ttl;
    }
    
    void dec_ttl()
    {
        if (ttl == 0)
            throw FatalException("");
        ttl--;
    }

    
    uint8_t get_flags() const {
        return flags;
    }

    const UUID& get_source_uuid() const {
        return source_uuid;
    }
    const string& get_node_address() const {
        return node_address;
    }

    const char* get_group_name() const {
        return group_name.c_str();
    }

    const NodeList* get_node_list() const {
        return node_list;
    }

};

class GMCastProto;
typedef multimap<const UUID, string> UUIDToAddressMap;

class GMCast : public Transport, EventContext
{
    UUID uuid;

    /* */
    typedef map<const int, GMCastProto*> ProtoMap;
    inline GMCastProto* get_gmcast_proto(ProtoMap::iterator i)
    {
        return i->second;
    }

    const GMCastProto* get_gmcast_proto(ProtoMap::const_iterator& i) const
    {
        return i->second;
    }

    ProtoMap proto_map;
    ProtoMap spanning_tree;

    /* */
    Transport* listener;
    string listen_addr;

    struct Timing
    {
        Time last_seen;
        Time next_reconnect;
        int retry_cnt;
        Timing(const Time& last_seen_, const Time& next_reconnect_) :
            last_seen(last_seen_),
            next_reconnect(next_reconnect_),
            retry_cnt(0)
        {

        }
    };
    
    typedef map<const string, Timing > AddrList;
    AddrList remote_addrs;

    const string& get_address(const AddrList::const_iterator& i) const
    {
        return i->first;
    }

    void set_last_seen(AddrList::iterator& i, const Time& t)
    {
        i->second.last_seen = t;
    }

    const Time& get_last_seen(AddrList::const_iterator& i) const
    {
        return i->second.last_seen;
    }
    

    void set_next_reconnect(AddrList::iterator& i, const Time& t)
    {
        i->second.next_reconnect = t;
    }

    const Time& get_next_reconnect(AddrList::const_iterator& i) const
    {
        return i->second.next_reconnect;
    }

    int get_retry_cnt(AddrList::iterator& i)
    {
        return i->second.retry_cnt;
    }

    int get_retry_cnt(AddrList::const_iterator& i) const
    {
        return i->second.retry_cnt;
    }

    void set_retry_cnt(AddrList::iterator& i, const int cnt)
    {
        i->second.retry_cnt = cnt;
    }


    string group_name;
    
    /* Accept a new connection */
    void gmcast_accept();
    /* Connect to remote host */
    void gmcast_connect(const string&);
    /* Handle GMCastProto that has connected succesfully to remote host */
    void handle_connected(GMCastProto*);
    /* Handle GMCastProto that has finished handshake sequence */
    void handle_established(GMCastProto*);
    /* Handle GMCastProto that has failed */
    void handle_failed(GMCastProto*);
    /* Remote proto entry */
    void remove_proto(const int);
    
    bool addr_connected(const string& addr) const;
    void insert_address(const string& addr);
    void update_addresses();
    void reconnect();


    void compute_spanning_tree(const UUIDToAddressMap&);
    void forward_message(const int cid, const ReadBuf* rb, 
                         const size_t offset, const GMCastMessage& msg);

    
    /* Start gmcast engine */
    void start();
    /* Stop gmcast engine */
    void stop();

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
        return uuid;
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
