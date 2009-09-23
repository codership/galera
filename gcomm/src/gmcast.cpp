#include "gmcast.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"
#include "gcomm/map.hpp"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/kruskal_min_spanning_tree.hpp>

using std::pair;
using std::make_pair;


class gcomm::GMCastProtoMap : 
    public Map<const int, GMCastProto*, std::map<const int, GMCastProto*> >
{};

class gcomm::UUIDToAddressMap :
    public MultiMap<const UUID, string, std::multimap<const UUID, string> >
{};

class gcomm::GMCastNode
{
    bool    operational;
    uint8_t weight;
    UUID    uuid;
    static const size_t ADDR_SIZE = 64;
    char    address[ADDR_SIZE];

public:

    GMCastNode()
        : 
        operational(false), 
        weight(0xff), 
        uuid() 
    {
        memset(address, 0, sizeof(address));
    }
    
    GMCastNode(const bool    operational_, 
               const UUID&   uuid_, 
               const string& address_)
        :
        operational (operational_), 
        weight      (0xff),
        uuid        (uuid_)
    {
        if (address_.size() > sizeof(address) - 1)
        {
            gcomm_throw_fatal << "Address too big: " << address_.size()
                              << " > " << (sizeof(address));
        }

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
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset) {
        size_t off;
        uint8_t byte;
        if ((off = gcomm::read(buf, buflen, offset, &byte)) == 0)
            return 0;
        operational = byte & 0x1;
        if ((off = uuid.read(buf, buflen, off)) == 0)
            return 0;
        if (off + ADDR_SIZE > buflen)
            return 0;
        memcpy(address, buf + off, ADDR_SIZE);
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

    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const {
        size_t off;
        uint8_t byte = operational ? 0x1 : 0;
        if ((off = gcomm::write(byte, buf, buflen, offset)) == 0)
            return 0;
        if ((off = uuid.write(buf, buflen, off)) == 0)
            return 0;
        if (off + ADDR_SIZE > buflen)
            return 0;
        memcpy(buf + off, address, ADDR_SIZE);
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

class gcomm::GMCastMessage
{
    byte_t version;
    byte_t type;
    byte_t flags;
    byte_t ttl;
    UUID   source_uuid;
    string node_address;
    string group_name;

    GMCastMessage(const GMCastMessage&);
    GMCastMessage& operator=(const GMCastMessage&);

public:

    typedef std::list<GMCastNode> NodeList;

private:

    NodeList* node_list;

public:
    
    enum Flags {
        F_GROUP_NAME   = 1 << 0,
        F_NODE_NAME    = 1 << 1,
        F_NODE_ADDRESS = 1 << 2,
        F_NODE_LIST    = 1 << 3
    };
    
    enum PacketType 
    {
        P_INVALID            = 0,
        P_HANDSHAKE          = 1,
        P_HANDSHAKE_RESPONSE = 2,
        P_HANDSHAKE_OK       = 3,
        P_HANDSHAKE_FAIL     = 4,
        P_TOPOLOGY_CHANGE    = 5,
        /* Leave room for future use */
        P_USER_BASE          = 8
    };
    
    /* Default ctor */
    GMCastMessage ()
        :
        version      (0), 
        type         (0), 
        flags        (0), 
        ttl          (0), 
        source_uuid  (), 
        node_address (),
        group_name   (),
        node_list    (0)
    {}
    
    /* Ctor for handshake, handshake ok and handshake fail */
    GMCastMessage (const uint8_t type_,
                   const UUID&   source_uuid_)
        :
        version      (0), 
        type         (type_), 
        flags        (0), 
        ttl          (1), 
        source_uuid  (source_uuid_), 
        node_address (),
        group_name   (),
        node_list    (0)
    {
        if (type != P_HANDSHAKE && type != P_HANDSHAKE_OK && 
            type != P_HANDSHAKE_FAIL)
            gcomm_throw_fatal << "Invalid message type " << type
                              << " in handshake constructor";        
    }
    
    /* Ctor for user message */
    GMCastMessage (const uint8_t type_,
                   const UUID&   source_uuid_, 
                   const uint8_t ttl_)
        :
        version      (0), 
        type         (type_), 
        flags        (0), 
        ttl          (ttl_), 
        source_uuid  (source_uuid_), 
        node_address (),
        group_name   (""),
        node_list    (0)
    {
        if (type < P_USER_BASE)
            gcomm_throw_fatal << "Invalid message type" << type
                              << " in user message constructor";
    }
    
    /* Ctor for handshake response */
    GMCastMessage (const uint8_t type_,
                   const UUID&   source_uuid_,
                   const string& node_address_,
                   const string& group_name_)
        : 
        version      (0),
        type         (type_), 
        flags        (F_GROUP_NAME | F_NODE_ADDRESS), 
        ttl          (1),
        source_uuid  (source_uuid_),
        node_address (node_address_),
        group_name   (group_name_),
        node_list    (0)

    {
        if (type != P_HANDSHAKE_RESPONSE)
            gcomm_throw_fatal << "Invalid message type " << type
                              << " in handshake response constructor";
    }

    /* Ctor for topology change */
    GMCastMessage (const uint8_t   type_, 
                   const UUID&     source_uuid_,
                   const string&   group_name_,
                   const NodeList& nodes)
        :
        version      (0),
        type         (type_),
        flags        (F_GROUP_NAME | F_NODE_LIST), 
        ttl          (1),
        source_uuid  (source_uuid_),
        node_address (),
        group_name   (group_name_),
        node_list    (new NodeList(nodes))
    {
        if (type != P_TOPOLOGY_CHANGE)
            gcomm_throw_fatal << "Invalid message type " << type
                              << " in topology change constructor";
    }
    
    ~GMCastMessage() 
    {
        delete node_list;
    }

    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
    {
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
    
    size_t read_v0(const byte_t* buf, const size_t buflen, const size_t offset)
    {
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
#if 0
            char* addr = 0;
            if ((off = read_string(buf, buflen, off, &addr)) == 0)
                return 0;
            node_address = addr;
            free(addr);
#endif
            node_address = reinterpret_cast<const char*>(buf + off);
            off += node_address.length() + 1;
        }
        
        if (flags & F_GROUP_NAME) 
        {
#if 0
            char* grp = 0;
            if ((off = read_string(buf, buflen, off, &grp)) == 0)
                return 0;
            group_name = grp;
            free(grp);
#endif
            group_name = reinterpret_cast<const char*>(buf + off);
            off += group_name.length() + 1;
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

        log_debug << "type: " << type << ", flags: " << flags
                  << ", ttl: " << ttl
                  << ", node_address: " << (F_NODE_ADDRESS ? node_address : "")
                  << ", group_name: "   << (F_GROUP_NAME   ? group_name   : "");

        return off;
    }
    
    size_t read(const byte_t* buf, const size_t buflen, const size_t offset) {
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
        if (ttl == 0) gcomm_throw_fatal << "decrementing 0 ttl";
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

    const string& get_group_name() const {
        return group_name;
    }

    const NodeList* get_node_list() const {
        return node_list;
    }

};


BEGIN_GCOMM_NAMESPACE

#if 0
const UUID& get_uuid(UUIDToAddressMap::const_iterator i)
{
    return i->first;
}
#endif // 0

const string& get_address(UUIDToAddressMap::const_iterator i)
{
    return i->second;
}


static bool exists(const UUIDToAddressMap& uuid_map, 
                   const UUID& uuid,
                   const string& addr)
{
    pair<UUIDToAddressMap::const_iterator, 
        UUIDToAddressMap::const_iterator> 
        ret = uuid_map.equal_range(uuid);
    for (UUIDToAddressMap::const_iterator i = ret.first; 
         i != ret.second; ++i)
    {
        if (get_address(i) == addr)
            return true;
    }
    return false;
}

static bool equals(const UUIDToAddressMap& a, 
                   const UUIDToAddressMap& b)
{
    if (a.size() != b.size())
        return false;
    for (UUIDToAddressMap::const_iterator i = a.begin(); i != a.end(); ++i)
    {
        if (exists(b, get_uuid(i), get_address(i)) == false)
            return false;
    }
    return true;
}

static void set_tcp_defaults (URI* uri)
{
    // what happens if there is already this parameter?
    uri->set_query_param(Conf::TcpParamNonBlocking, gu::to_string(1));
}

class GMCastProto 
{
public:
    
    enum State 
    {
        S_INIT,
        S_HANDSHAKE_SENT,
        S_HANDSHAKE_WAIT,
        S_HANDSHAKE_RESPONSE_SENT,
        S_OK,
        S_FAILED,
        S_CLOSED
    };

private:

    UUID    local_uuid;
    UUID    remote_uuid;
    string  local_addr;
    string  remote_addr;
    string  group_name;
    uint8_t send_ttl;
    bool    changed;
    State   state;

    Transport*       tp;
    UUIDToAddressMap uuid_map;

    GMCastProto(const GMCastProto&);
    GMCastProto& operator=(const GMCastProto&);

public:
    
    State get_state() const 
    {
        return state;
    }

    static string to_string (State s) 
    {
        switch (s)
        {
        case S_INIT:                    return "INIT";
        case S_HANDSHAKE_SENT:          return "HANDSHAKE_SENT";
        case S_HANDSHAKE_WAIT:          return "HANDSHAKE_WAIT";
        case S_HANDSHAKE_RESPONSE_SENT: return "HANDSHAKE_RESPONSE_SENT";
        case S_OK:                      return "OK";
        case S_FAILED:                  return "FAILED";
        case S_CLOSED:                  return "CLOSED";
        default: return "UNKNOWN";
        }
    }
    
    void set_state(State new_state) 
    {
        log_debug << "State change: " << to_string(state) << " -> " 
                  << to_string(new_state);

        static const bool allowed[][7] =
        {
            // INIT  HS_SENT HS_WAIT HSR_SENT   OK    FAILED CLOSED
            { false,  true,   true,   false,  false,  false, false },// INIT

            { false,  false,  false,  false,  true,   true,  false },// HS_SENT

            { false,  false,  false,  true,   false,  false, false },// HS_WAIT

            { false,  false,  false,  false,  true,   true,  false },// HSR_SENT

            { false,  false,  false,  false,  false,  true,  true  },// OK

            { false,  false,  false,  false,  false,  false, true  },// FAILED

            { false,  false,  false,  false,  false,  false, false } // CLOSED
        };

        if (!allowed[state][new_state])
        {
            gcomm_throw_fatal << "Invalid state change: " << to_string(state)
                              << " -> " << to_string(new_state);
        }

        state = new_state;
    }
    
    GMCastProto (Transport*    tp_, 
                 const string& local_addr_, 
                 const string& remote_addr_, 
                 const UUID&   local_uuid_, 
                 const string& group_name_)
        : 
        local_uuid  (local_uuid_),
        remote_uuid (),
        local_addr  (local_addr_),
        remote_addr (remote_addr_),
        group_name  (group_name_),
        send_ttl    (1),
        changed     (false),
        state       (S_INIT),
        tp          (tp_),
        uuid_map    ()
    {}

    ~GMCastProto() 
    {
        // tp->close();
        // delete tp;
    }

    void send_msg(const GMCastMessage& msg)
    {
        byte_t* buf = new byte_t[msg.size()];

        if (msg.write(buf, msg.size(), 0) == 0) 
        {
            delete[] buf;
            gcomm_throw_fatal << "Message serialization";
        }

        WriteBuf wb(buf, msg.size());

        int ret = tp->handle_down(&wb, 0);

        if (ret)
        {
            log_debug << "Send failed: " << strerror(ret);
            set_state(S_FAILED);
        }

        delete[] buf;
    }
    
    void send_handshake() 
    {
        GMCastMessage hs (GMCastMessage::P_HANDSHAKE, local_uuid);

        send_msg(hs);

        set_state(S_HANDSHAKE_SENT);
    }
    
    void wait_handshake() 
    {
        if (get_state() != S_INIT)
            gcomm_throw_fatal << "Invalid state: " << to_string(get_state());

        set_state(S_HANDSHAKE_WAIT);
    }
    
    void handle_handshake(const GMCastMessage& hs) 
    {
        if (get_state() != S_HANDSHAKE_WAIT)
            gcomm_throw_fatal << "Invalid state: " << to_string(get_state());

        remote_uuid = hs.get_source_uuid();

        GMCastMessage hsr (GMCastMessage::P_HANDSHAKE_RESPONSE, 
                           local_uuid, 
                           local_addr,
                           group_name);
        send_msg(hsr);

        set_state(S_HANDSHAKE_RESPONSE_SENT);
    }

    void handle_handshake_response(const GMCastMessage& hs) 
    {
        if (get_state() != S_HANDSHAKE_SENT)
            gcomm_throw_fatal << "Invalid state: " << to_string(get_state());
            
        const string& grp = hs.get_group_name();

        log_debug << "hsr: " << hs.get_source_uuid().to_string() << " @ "
                  << hs.get_node_address() << " / " << grp;

        try
        {
            if (grp != group_name)
            {
                log_debug << "Handshake failed, my group: '" << group_name
                          << "', peer group: '" << grp << "'";
                throw false;
            }

            remote_uuid = hs.get_source_uuid();

            URI uri(hs.get_node_address());

            remote_addr = uri.get_scheme() + "://";

            try
            {
                if (0 == uri.get_host().length()) throw gu::NotSet();

                remote_addr += uri.get_host();
            }
            catch (gu::NotSet&)
            {
                remote_addr += tp->get_remote_host();
            }

            try
            {
                remote_addr += (string(":") + uri.get_port());
            }
            catch (gu::NotSet&) {}

            GMCastMessage ok(GMCastMessage::P_HANDSHAKE_OK, local_uuid);

            send_msg(ok);
            set_state(S_OK);
        }
        catch (...)
        {
            log_warn << "Parsing peer address '"
                     << hs.get_node_address() << "' failed.";

            GMCastMessage nok (GMCastMessage::P_HANDSHAKE_FAIL, local_uuid);

            send_msg (nok);
            set_state(S_FAILED);
        }
    }

    void handle_ok(const GMCastMessage& hs) 
    {
        set_state(S_OK);
    }
    
    void handle_failed(const GMCastMessage& hs) 
    {
        set_state(S_FAILED);
    }
    

    void handle_topology_change(const GMCastMessage& msg)
    {
        const GMCastMessage::NodeList* nl = msg.get_node_list();
        if (nl == 0)
        {
            LOG_WARN("null node list");
        }

        UUIDToAddressMap new_map;
        for (GMCastMessage::NodeList::const_iterator i = nl->begin();
             i != nl->end(); ++i)
        {
            if (exists(new_map, i->get_uuid(), i->get_address()))
            {
                LOG_WARN("Duplicate entry");
                continue;
            }
            new_map.insert(make_pair(i->get_uuid(), i->get_address()));
        }
        
        if (equals(uuid_map, new_map) == false)
        {
            LOG_DEBUG("topology change");
            changed = true;
        }
        uuid_map = new_map;
    }

    void send_topology_change(UUIDToAddressMap& um)
    {
        GMCastMessage::NodeList nl;
        for (UUIDToAddressMap::const_iterator i = um.begin(); i != um.end(); ++i)
        {
            if (i->first == UUID() || i->second == string(""))
                gcomm_throw_fatal << "nil uuid or empty address";

            nl.push_back(GMCastNode(true, i->first, i->second));
        }

        GMCastMessage msg(GMCastMessage::P_TOPOLOGY_CHANGE, local_uuid,
                          group_name, nl);
        
        send_msg(msg);
    }


    void handle_user(const GMCastMessage& hs) //? strange function
    {
        if (get_state() != S_OK)
            gcomm_throw_fatal << "invalid state";

        if (hs.get_type() < GMCastMessage::P_USER_BASE)
            gcomm_throw_fatal << "invalid user message";

        gcomm_throw_fatal;
    }
    
    void handle_message(const GMCastMessage& msg) 
    {


        log_debug << "message type: " << msg.get_type();

        switch (msg.get_type()) {
        case GMCastMessage::P_HANDSHAKE:
            handle_handshake(msg);
            break;
        case GMCastMessage::P_HANDSHAKE_RESPONSE:
            handle_handshake_response(msg);
            break;
        case GMCastMessage::P_HANDSHAKE_OK:
            handle_ok(msg);
            break;
        case GMCastMessage::P_HANDSHAKE_FAIL:
            handle_failed(msg);
            break;
        case GMCastMessage::P_TOPOLOGY_CHANGE:
            handle_topology_change(msg);
            break;
        default:
            handle_user(msg);
        }

    }

    const UUID& get_local_uuid() const 
    {
        return local_uuid;
    }

    const UUID& get_remote_uuid() const
    {
        return remote_uuid;
    }

    Transport* get_transport() const
    {
        return tp;
    }

    uint8_t get_send_ttl() const
    {
        return send_ttl;
    }

    void set_send_ttl(const uint8_t t)
    {
        send_ttl = t;
    }


    const string& get_remote_addr() const
    {
        return remote_addr;
    }

    const UUIDToAddressMap& get_uuid_map() const
    {
        return uuid_map;
    }
    

    bool get_changed()
    {
        bool ret = changed;
        changed = false;
        return ret;
    }
};

static bool check_uri(const URI& uri)
{
    return (uri.get_scheme() == Conf::TcpScheme);
}

static const string tcp_addr_prefix = Conf::TcpScheme + "://";

GMCast::GMCast(const URI& uri, EventLoop* event_loop, Monitor* mon)
    :
    Transport     (uri, event_loop, mon),
    my_uuid       (0, 0),
    proto_map     (new ProtoMap()),
    spanning_tree (new ProtoMap()),
    listener      (0),
    listen_addr   (tcp_addr_prefix),
    initial_addr  (""),
    pending_addrs (),
    remote_addrs  (),
    group_name    ()
{
    if (uri.get_scheme() != Conf::GMCastScheme)
    {
        gcomm_throw_runtime (EINVAL) << "Invalid uri scheme: "
                                     << uri.get_scheme();
    }

    // @todo: technically group name should be in path component
    try
    {
        group_name = uri.get_option (Conf::GMCastQueryGroup);
    }
    catch (gu::NotFound&)
    {
        gcomm_throw_runtime (EINVAL) << "Group not defined in URL: "
                                     << uri.to_string();
    }

    try
    {
        if (!host_undefined(uri.get_host()))
        {
            initial_addr = tcp_addr_prefix + uri.get_authority();
            log_debug << "Setting initial_addr to '" << initial_addr << "'";
        }
    }
    catch (gu::NotSet&)
    {
        //@note: this is different from empty host and indicates URL without ://
        gcomm_throw_runtime (EINVAL) << "Host not defined in URL: "
                                     << uri.to_string();
    }

    try
    {
        listen_addr = uri.get_option (Conf::GMCastQueryListenAddr);
        log_debug << "Setting listen address to " << listen_addr;
    }
    catch (gu::NotFound&) {}

    try
    { 
        gu::URI(listen_addr).get_port();
    }
    catch (gu::NotSet&)
    {
        // if no port is set for listen address in the options,
        // try one from authority part
        try
        {
            if (uri.get_port().length()) listen_addr += ':' + uri.get_port();
        }
        catch (gu::NotSet&) {}
    }

    log_info << "Listening at: " << listen_addr;

    if (check_uri(listen_addr) == false)
    {
        gcomm_throw_runtime (EINVAL) << "Listen addr '" << listen_addr
                                     << "' is not valid";
    }
    
    fd = PseudoFd::alloc_fd();
}

GMCast::~GMCast()
{
    if (listener != 0) stop();

    PseudoFd::release_fd(fd);
    delete proto_map;
    delete spanning_tree;
}

void GMCast::start() 
{    
    URI listen_uri(listen_addr);

    set_tcp_defaults (&listen_uri);
    
    listener = Transport::create(listen_uri, event_loop);
    listener->listen();
    listener->set_up_context(this, listener->get_fd());

    log_debug << "Listener: " << listener->get_fd();

    if (initial_addr != "")
    {
        log_debug << "Connecting to: " << initial_addr;
        insert_address(initial_addr, UUID(), pending_addrs);
        gmcast_connect(initial_addr);
    }

    event_loop->insert(fd, this);
    event_loop->queue_event(fd, Event(Event::E_USER,
                                      Time::now() + Time(0, 500000)));
}


void GMCast::stop() 
{
    event_loop->erase(fd);
    
    listener->close();
    delete listener;
    listener = 0;    
    spanning_tree->clear();

    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        Transport* tp = ProtoMap::get_value(i)->get_transport();
        tp->close();
        delete tp;
        delete ProtoMap::get_value(i);
    }

    proto_map->clear();
    pending_addrs.clear();
    remote_addrs.clear();
}


void GMCast::gmcast_accept() 
{
    Transport* tp = listener->accept();

    tp->set_up_context(this, tp->get_fd());

    GMCastProto* peer = new GMCastProto (tp, listen_addr, "",
                                         get_uuid(), group_name);

    pair<ProtoMap::iterator, bool> ret =
        proto_map->insert(make_pair(tp->get_fd(), peer));

    if (ret.second == false)
    {
        delete peer;
        gcomm_throw_fatal << "Failed to add peer to map";
    }

    peer->send_handshake();
}

void GMCast::gmcast_connect(const string& remote_addr)
{
    if (remote_addr == listen_addr) return;

    URI connect_uri(remote_addr);

    set_tcp_defaults (&connect_uri);

    Transport* tp = Transport::create(connect_uri, event_loop);

    try 
    {
        tp->connect();
    }
    catch (RuntimeException e)
    {
        log_warn << "Connect failed: " << e.what();
        delete tp;
        return;
    }
    
    tp->set_up_context(this, tp->get_fd());

    GMCastProto* peer = new GMCastProto (tp, listen_addr, remote_addr,
                                         get_uuid(), group_name);

    pair<ProtoMap::iterator, bool> ret = 
        proto_map->insert(make_pair(tp->get_fd(), peer));

    if (ret.second == false)
    {
        delete peer;
        gcomm_throw_fatal << "Failed to add peer to map";
    }

    ret.first->second->wait_handshake();
}

void GMCast::gmcast_forget(const UUID& uuid)
{
    /* Close all proto entries corresponding to uuid */
    
    ProtoMap::iterator pi, pi_next;
    for (pi = proto_map->begin(); pi != proto_map->end(); pi = pi_next)
    {
        pi_next = pi, ++pi_next;
        GMCastProto* rp = ProtoMap::get_value(pi);
        if (rp->get_remote_uuid() == uuid)
        {
            rp->get_transport()->close();
            event_loop->release_protolay(rp->get_transport());
            delete rp;
            proto_map->erase(pi);
        }
    }
    
    /* Set all corresponding entries in address list to have retry cnt 
     * max_retry_cnt + 1 and next reconnect time after some period */
    
    AddrList::iterator ai;
    for (ai = remote_addrs.begin(); ai != remote_addrs.end(); ++ai)
    {
        if (get_uuid(ai) == uuid)
        {
            set_retry_cnt(ai, max_retry_cnt + 1);
            set_next_reconnect(ai, Time::now() + Time(5, 0));
        }
    }
    
    /* Update state */
    update_addresses();
}

void GMCast::handle_connected(GMCastProto* rp)
{
    const Transport* tp = rp->get_transport();
    LOG_DEBUG("transport " + make_int(tp->get_fd()).to_string() + " connected");

    

}

void GMCast::handle_established(GMCastProto* rp)
{
    log_info << self_string() << " connection established to "
             << rp->get_remote_uuid().to_string() << " "
             << rp->get_remote_addr();
    AddrList::iterator i = pending_addrs.find(rp->get_remote_addr());
    if (i != pending_addrs.end())
    {
        pending_addrs.erase(i);
    }
    
    if ((i = remote_addrs.find(rp->get_remote_addr())) == remote_addrs.end())
    {
        insert_address(rp->get_remote_addr(), rp->get_remote_uuid(),
                       remote_addrs);
        i = remote_addrs.find(rp->get_remote_addr());
    }
    
    set_retry_cnt(i, -1);
    
}

void GMCast::handle_failed(GMCastProto* rp)
{
    Transport* tp = rp->get_transport();
    if (tp->get_state() == S_FAILED) 
    {
        LOG_DEBUG(string("transport ") + make_int(tp->get_fd()).to_string() 
                  + " failed: " + ::strerror(tp->get_errno()));
    } 
    else 
    {
        LOG_WARN(string("transport ") + make_int(tp->get_fd()).to_string() 
                 + " in unexpected state " + make_int(tp->get_errno()).to_string());
    }
    tp->close();
    event_loop->release_protolay(tp);
    
    const string& remote_addr = rp->get_remote_addr();
    if (remote_addr != "")
    {
        AddrList::iterator i;

        if ((i = pending_addrs.find(remote_addr)) != pending_addrs.end() ||
            (i = remote_addrs.find(remote_addr)) != remote_addrs.end())
        {
            set_retry_cnt(i, get_retry_cnt(i) + 1);
            int rsecs = 1;
            Time rtime = Time::now() + Time(rsecs, 0);
            log_debug << "setting next reconnect time to "
                      << rtime.to_string() << " for " << remote_addr;
            set_next_reconnect(i, rtime);
        }
    }
    
    delete rp;
}

void GMCast::remove_proto(const int fd)
{

    proto_map->erase(fd);
    spanning_tree->erase(fd);
}



bool GMCast::is_connected(const string& addr, const UUID& uuid) const
{
    for (ProtoMap::const_iterator i = proto_map->begin();
         i != proto_map->end(); ++i)
    {
        if (addr == ProtoMap::get_value(i)->get_remote_addr() || 
            uuid == ProtoMap::get_value(i)->get_remote_uuid())
            return true;
    }
    return false;
}

void GMCast::insert_address (const string& addr,
                             const UUID&   uuid,
                             AddrList&     alist)
{
    if (addr == listen_addr)
    {
        gcomm_throw_fatal << "Trying to add self to addr list";
    }

    if (alist.insert(make_pair(addr, 
                               Timing(Time::now(),
                                      Time::now(), uuid))).second == false)
    {
        log_warn << "Duplicate entry: " << addr;
    }
    else
    {
        log_debug << self_string() << ": new address entry " << uuid.to_string()
                  << ' ' << addr;
    }
}

using namespace boost;
using std::vector;

typedef adjacency_list< listS, vecS, undirectedS,
                        property<vertex_index_t, UUID>, 
                        property<edge_weight_t,  int> > Graph;

typedef graph_traits <Graph>::edge_descriptor   Edge;
typedef graph_traits <Graph>::vertex_descriptor Vertex;
typedef pair<int, int> E;


static inline int find_safe(const map<const UUID, int>& m, const UUID& val)
{
    map<const UUID, int>::const_iterator i = m.find(val);

    if (i == m.end())
    {
        gcomm_throw_fatal << "Missing UUID " << val.to_string();
    }

    return i->second;
}

static inline const UUID& find_safe(const map<const int, UUID>& m,
                                    const int val)
{
    map<const int, UUID>::const_iterator i = m.find(val);

    if (i == m.end()) gcomm_throw_fatal << "Not found: " << val;

    return i->second;
}

void GMCast::compute_spanning_tree(const UUIDToAddressMap& uuid_map)
{
    /* Construct mapping between indexing [0, n) and UUIDs, as well as
     * between UUIDs and proto map entries */
    map<const UUID, int> uuid_to_idx;
    map<const int, UUID> idx_to_uuid;
    map<const UUID, pair<const int, GMCastProto*> > uuid_to_proto;
    
    if (uuid_to_idx.insert(pair<const UUID, int>(get_uuid(), 0)).second ==
        false)
        gcomm_throw_fatal << "Insert to uuid_to_idx failed";

    if (idx_to_uuid.insert(pair<const int, UUID>(0, get_uuid())).second ==
        false)
        gcomm_throw_fatal << "Insert to idx_to_uuid failed";

    int n = 1;

    for (UUIDToAddressMap::const_iterator i = uuid_map.begin();
         i != uuid_map.end(); ++i)
    {
        if (uuid_to_idx.insert(make_pair(i->first, n)).second == true)
        {
            if (idx_to_uuid.insert(make_pair(n, i->first)).second == false)
                gcomm_throw_fatal;
            ++n;
        }
    }
    
    /* Construct lists of edges and weights */
    list<E>   edges;
    list<int> weights;

    for (ProtoMap::const_iterator i = proto_map->begin(); i != proto_map->end();
         ++i)
    {
        if (i->second->get_state() != GMCastProto::S_OK)
        {
            continue;
        }
        
        uuid_to_proto.insert(make_pair(
                                 i->second->get_remote_uuid(), 
                                 make_pair(i->first, i->second)));
        edges.push_back(
            E(find_safe(uuid_to_idx, i->second->get_local_uuid()), 
              find_safe(uuid_to_idx, i->second->get_remote_uuid())));
        weights.push_back(1);
        
        for (UUIDToAddressMap::const_iterator j = i->second->get_uuid_map().begin(); j != i->second->get_uuid_map().end(); ++j)
        {
            if (j->first != i->second->get_local_uuid())
            {
                edges.push_back(
                    E(find_safe(uuid_to_idx, i->second->get_remote_uuid()), 
                      find_safe(uuid_to_idx, j->first)));
                weights.push_back(2);
            }
        }
    }
    
    /* Create graph */
    Graph graph(edges.begin(), edges.end(), weights.begin(), n);
 
    /* Compute minimum spanning tree */
    list<Edge> st;
    kruskal_minimum_spanning_tree(graph, std::back_inserter(st));

    /* Reset spanning_tree and proto map states */
    spanning_tree->clear();
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        i->second->set_send_ttl(1);
    }
    
    /* Scan through list of edges and construct spanning_tree accordingly, 
     * if source vertex is self. */
    list<Edge>::iterator ei, ei_next;
    for (ei = st.begin(); ei != st.end(); ei = ei_next)
    {
        ei_next = ei, ++ei_next;
        const UUID& source_uuid = find_safe(idx_to_uuid, source(*ei, graph));
        
        if (source_uuid == get_uuid())
        {
            // Edge start vertex is self
            
            map<const UUID, pair<const int, GMCastProto*> >::const_iterator
                up_target = 
                uuid_to_proto.find(find_safe(idx_to_uuid, target(*ei, graph)));
            
            if (up_target != uuid_to_proto.end())
            {
                // std::cerr << up_target->second.first << " ";
                if (spanning_tree->insert(
//                        make_pair(up_target->second.first, 
//                                  up_target->second.second)).second == false)
                        up_target->second).second == false)
                    gcomm_throw_fatal << "Adding connection to spanning tree"
                                      << " failed";
            }

            st.erase(ei);
        }
    }
    
    /* Scan through remaining entries and find out suitable 
     * spanning_tree entry for outgoing route. */
    for (ei = st.begin(); ei != st.end(); ++ei)
    {
        log_debug << "multihop route detected, looking up for proper route";
        const UUID& source_uuid = find_safe(idx_to_uuid, source(*ei, graph));
        ProtoMap::iterator i;
        for (i = spanning_tree->begin(); i != spanning_tree->end(); ++i)
        {
            if (i->second->get_remote_uuid() == source_uuid)
            {
                i->second->set_send_ttl(2);
                break;
            }
        }
        if (i == spanning_tree->end())
        {
            log_warn << "no outgoing route found for "
                     << source_uuid.to_string();
        }
    }

    if (st.begin() == st.end())
    {
        log_debug << self_string() << " single hop spanning tree of size "
                  << spanning_tree->size();
    }
    else
    {
        log_debug << self_string() << " multi hop spanning tree of size "
                  << spanning_tree->size();
    }

}

void GMCast::update_addresses()
{
    UUIDToAddressMap uuid_map;

    /* Add all established connections into uuid_map and update 
     * list of remote addresses */
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        GMCastProto* rp = ProtoMap::get_value(i);

        if (rp->get_state() == GMCastProto::S_OK)
        {
            if (rp->get_remote_addr() == "" || rp->get_remote_uuid() == UUID())
            {
                gcomm_throw_fatal << "Protocol error: local: " 
                                  << get_uuid().to_string() << " "
                                  << listen_addr << ", remote: "
                                  << rp->get_remote_uuid().to_string()
                                  << " '" << rp->get_remote_addr() << "'";
            }
            
            if (exists(uuid_map, rp->get_remote_uuid(), 
                       rp->get_remote_addr()) == false)
            {
                uuid_map.insert(make_pair(rp->get_remote_uuid(), 
                                          rp->get_remote_addr()));
            }
            
            if (remote_addrs.find(rp->get_remote_addr()) == remote_addrs.end())
            {
                log_warn << "proto exists but no addr on addr list for "
                         << rp->get_remote_addr();
                insert_address(rp->get_remote_addr(), rp->get_remote_uuid(), 
                               remote_addrs);
            }
        }
    }
    
    /* Send topology change message containing only established 
     * connections */
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        GMCastProto* gp = ProtoMap::get_value(i);
        if (gp->get_state() == GMCastProto::S_OK)
            gp->send_topology_change(uuid_map);
    }

    /* Add entries reported by all other nodes to get complete view 
     * of existing uuids/addresses */
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        GMCastProto* rp = ProtoMap::get_value(i);

        if (rp->get_state() == GMCastProto::S_OK)
        {
            for (UUIDToAddressMap::const_iterator j=rp->get_uuid_map().begin();
                 j != rp->get_uuid_map().end(); ++j)
            {
                if (j->second == "" || j->first == UUID())
                {
                    gcomm_throw_fatal << "Protocol error: local: "
                                      << get_uuid().to_string()
                                      << " " << listen_addr
                                      << ", remote: "
                                      << rp->get_remote_uuid().to_string()
                                      << " " << rp->get_remote_addr() << " "
                                      << j->first.to_string() << " "
                                      << j->second;
                }

                if (exists(uuid_map, j->first, j->second) == false)
                {
                    uuid_map.insert(make_pair(j->first, j->second));
                }

                if (j->first != get_uuid() &&
                    remote_addrs.find(j->second) == remote_addrs.end() &&
                    pending_addrs.find(j->second) == pending_addrs.end())
                {
                    log_debug << "proto refers but no addr on addr list for "
                              << j->second;
                    insert_address(j->second, j->first, pending_addrs);
                    set_retry_cnt(pending_addrs.find(j->second),
                                  max_retry_cnt - 60);
                }
            }
        }
    }
    
    /* Compute spanning tree */
    compute_spanning_tree(uuid_map);
}


void GMCast::reconnect()
{
    /* Loop over known remote addresses and connect if proto entry 
     * does not exist */
    Time now = Time::now();
    AddrList::iterator i, i_next;

    for (i = pending_addrs.begin(); i != pending_addrs.end(); i = i_next)
    {
        i_next = i, ++i_next;

        if (is_connected(get_address(i), UUID()) == false)
        {
            if (get_next_reconnect(i) <= now)
            {
                gmcast_connect(get_address(i));
            }
            else if (get_retry_cnt(i) > max_retry_cnt)
            {
                pending_addrs.erase(i);
            }
        }
    }
    
    for (i = remote_addrs.begin(); i != remote_addrs.end(); i = i_next) 
    {
        if (get_uuid(i) == get_uuid())
        {
            gcomm_throw_fatal << "own uuid in addr list";
        }

        i_next = i, ++i_next;
        
        if (is_connected(get_address(i), get_uuid(i)) == false)
        {
            if (get_next_reconnect(i) <= now)
            {
                if (get_retry_cnt(i) > max_retry_cnt)
                {
                    log_info << self_string() 
                             << " erasing " << get_uuid(i).to_string() << " " 
                             << get_address(i);
                    remote_addrs.erase(i);
                }
                else 
                {
                    if (get_retry_cnt(i) % 60 == 0)
                    {
                        log_info << self_string() << " reconnecting " 
                                 << get_uuid(i).to_string() 
                                 << " " << get_address(i)
                                 << " attempt " << get_retry_cnt(i);
                    }
                    gmcast_connect(get_address(i));
                }
            }
            else
            {
                log_debug << "waiting reconnect to "
                          << get_uuid(i).to_string() << " "
                          << get_address(i) << " "
                          << get_next_reconnect(i).to_string() << " "
                          << now.to_string();
            }
        }
    }
}

void GMCast::handle_event(const int fd, const Event& pe) 
{
    Critical crit(mon);

    log_debug << "handle event";

    update_addresses();
    reconnect();
    
    event_loop->queue_event(fd, Event(Event::E_USER, 
                                    Time::now() + Time(0, 500000)));
}


void GMCast::forward_message(const int cid, const ReadBuf* rb, 
                             const size_t offset, const GMCastMessage& msg)
{
    // @todo: how about wb (rb, offset)?
    WriteBuf wb(rb->get_buf(offset), rb->get_len(offset));
    byte_t buf[20];
    size_t hdrlen;

    if ((hdrlen = msg.write(buf, sizeof(buf), 0)) == 0)
        gcomm_throw_fatal << "Write header failed";

    wb.prepend_hdr(buf, hdrlen);

    for (ProtoMap::iterator i = spanning_tree->begin();
         i != spanning_tree->end(); ++i)
    {
        if (i->first != cid)
        {
            log_debug << "Forwarding message "
                      << msg.get_source_uuid().to_string() << " -> "
                      << i->second->get_remote_uuid().to_string();

            i->second->get_transport()->handle_down(&wb, 0);
        }
    }

}

void GMCast::handle_up(const int    cid,    const ReadBuf* rb, 
                       const size_t offset, const ProtoUpMeta* um) 
{
    Critical crit(mon);

    if (listener == 0)
    {
        log_warn << "handle_up on non-listener";
        return;
    }

    if (cid == listener->get_fd()) 
    {
        gmcast_accept();
    } 
    else 
    {
        GMCastMessage msg;
        size_t        off;

        if (rb != 0)
        {
            if ((off = msg.read(rb->get_buf(), rb->get_len(), offset)) == 0)
                gcomm_throw_fatal << "message unserialization";

            if (msg.get_type() >= GMCastMessage::P_USER_BASE)
            {
                ProtoUpMeta um(msg.get_source_uuid());

                pass_up(rb, off, &um);

                if (msg.get_ttl() > 1)
                {
                    msg.dec_ttl();
                    forward_message(cid, rb, off, msg);
                }

                return;
            }
        }

        bool changed         = false;
        ProtoMap::iterator i = proto_map->find(cid);

        if (i == proto_map->end()) 
        {            
            log_warn << "unknown fd " << cid;
            return;
        }
        
        GMCastProto* rp = ProtoMap::get_value(i);

        if (rb == 0) 
        {
            
            if (rp->get_transport()->get_state() == S_CONNECTED) 
            {
                handle_connected(rp);
                changed = true;
            } 
            else 
            {
                handle_failed(rp);
                remove_proto(cid);
                changed = true;
            }
        } 
        else if (rp->get_state() != GMCastProto::S_FAILED &&
                 rp->get_state() != GMCastProto::S_CLOSED)
        {
            GMCastProto::State s = rp->get_state();

            rp->handle_message(msg);
            changed = changed || rp->get_changed();

            if (s != GMCastProto::S_OK && rp->get_state() == GMCastProto::S_OK)
            {
                handle_established(rp);
                changed = true;
            }
            else if (rp->get_state() == GMCastProto::S_FAILED)
            {
                handle_failed(rp);
                remove_proto(cid);
                changed = true;
            }
        }
        else
        {
            handle_failed(rp);
            remove_proto(cid);
            changed = true;
        }

        if (changed)
        {
            update_addresses();
        }

        gu_trace(reconnect());
    }
}

int GMCast::handle_down(WriteBuf* wb, const ProtoDownMeta* dm) 
{
    Critical crit(mon);
    
    for (ProtoMap::iterator i = spanning_tree->begin();
         i != spanning_tree->end(); ++i)
    {
        GMCastProto* rp = ProtoMap::get_value(i);
        GMCastMessage msg(GMCastMessage::P_USER_BASE, get_uuid(),
                          rp->get_send_ttl());
        byte_t hdrbuf[20];
        size_t wlen;

        if ((wlen = msg.write(hdrbuf, sizeof(hdrbuf), 0)) == 0)
            gcomm_throw_fatal << "short buffer";

        if (msg.get_ttl() > 1)
        {
            log_debug << "msg ttl: " << msg.get_ttl();
        }

        wb->prepend_hdr(hdrbuf, wlen);

        int err;
        
        if ((err = rp->get_transport()->handle_down(wb, 0)) != 0)
        {
            log_warn << "transport: " << ::strerror(err);
        }

        wb->rollback_hdr(wlen);
    }
    
    return 0;
}

END_GCOMM_NAMESPACE
