#include "gmcast.hpp"

#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/kruskal_min_spanning_tree.hpp>

using std::pair;
using std::make_pair;


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

static void set_tcp_params(URI* uri)
{
    uri->set_query_param(Conf::TcpParamNonBlocking, make_int(1).to_string());
}



class GMCastProto 
{
    UUID local_uuid;
    UUID remote_uuid;
    Transport* tp;
    string local_addr;
    string remote_addr;
    string group_name;
    uint8_t send_ttl;
    bool changed;
private:
    UUIDToAddressMap uuid_map;
    GMCastProto(const GMCastProto&);
    void operator=(const GMCastProto&);
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
    
    State state;
    
    State get_state() const 
    {
        return state;
    }

    string to_string(State s) 
    {
        switch (s)
        {
        case S_INIT:
            return "INIT";
        case S_HANDSHAKE_SENT:
            return "HANDSHAKE_SENT";
        case S_HANDSHAKE_WAIT:
            return "HANDSHAKE_WAIT";
        case S_HANDSHAKE_RESPONSE_SENT:
            return "HANDSHAKE_RESPONSE_SENT";
        case S_OK:
            return "OK";
        case S_FAILED:
            return "FAILED";
        case S_CLOSED:
            return "CLOSED";
        }
        return "UNKNOWN";
    }
    
    void set_state(State new_state) 
    {
        LOG_DEBUG(string("state change: ") 
                  + to_string(state) + " -> " 
                  + to_string(new_state));
        switch (state) {
        case S_INIT:
            if (new_state != S_HANDSHAKE_SENT &&
                new_state != S_HANDSHAKE_WAIT)
                throw FatalException("invalid state change");
            break;
        case S_HANDSHAKE_WAIT:
            if (new_state != S_HANDSHAKE_RESPONSE_SENT)
                throw FatalException("invalid state change");
            break;
        case S_HANDSHAKE_SENT:
        case S_HANDSHAKE_RESPONSE_SENT:
            if (new_state != S_OK && new_state != S_FAILED)
                throw FatalException("invalid state change");
            break;
        case S_OK:
            if (new_state != S_CLOSED && new_state != S_FAILED)
                throw FatalException("invalid state change");
            break;
        case S_FAILED:
            if (new_state != S_CLOSED)
                throw FatalException("invalid state change");
            break;
        case S_CLOSED:
            throw FatalException("invalid state change");
        }

        state = new_state;
    }
    
    GMCastProto(Transport* tp_, 
                const string& local_addr_, 
                const string& remote_addr_, 
                const UUID& local_uuid_, 
                const string& group_name_) : 
        local_uuid(local_uuid_),
        remote_uuid(),
        tp(tp_), 
        local_addr(local_addr_),
        remote_addr(remote_addr_),
        group_name(group_name_),
        send_ttl(1),
        changed(false),
        uuid_map(),
        state(S_INIT)
    {
    }

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
            throw FatalException("message serialization");
        }
        WriteBuf wb(buf, msg.size());
        int ret = tp->handle_down(&wb, 0);

        if (ret)
        {
            LOG_DEBUG(string("send failed: ") + strerror(ret));
            set_state(S_FAILED);
        }
        delete[] buf;
    }
    
    void send_handshake() 
    {
        GMCastMessage hs(GMCastMessage::P_HANDSHAKE, local_uuid);
        send_msg(hs);
        set_state(S_HANDSHAKE_SENT);
    }
    
    void wait_handshake() 
    {
        if (get_state() != S_INIT)
            throw FatalException("invalid state");
        set_state(S_HANDSHAKE_WAIT);
    }
    
    void handle_handshake(const GMCastMessage& hs) 
    {
        if (get_state() != S_HANDSHAKE_WAIT)
            throw FatalException("invalid state");
        remote_uuid = hs.get_source_uuid();
        GMCastMessage hsr(GMCastMessage::P_HANDSHAKE_RESPONSE, 
                         local_uuid, 
                         local_addr,
                         group_name.c_str());
        send_msg(hsr);
        set_state(S_HANDSHAKE_RESPONSE_SENT);
    }

    void handle_handshake_response(const GMCastMessage& hs) 
    {
        if (get_state() != S_HANDSHAKE_SENT)
            throw FatalException("handle_handshake_response(): invalid state");
            
        const char* grp = hs.get_group_name();
        LOG_DEBUG(string("hsr: ") + hs.get_source_uuid().to_string() + " " + 
                  hs.get_node_address() + " " + grp);
        if (grp == 0 || group_name != string(grp))
        {
            LOG_DEBUG(string("handshake fail, invalid group: ") + grp);
            GMCastMessage nok(GMCastMessage::P_HANDSHAKE_FAIL, local_uuid);
            send_msg(nok);
            set_state(S_FAILED);
        } 
        else 
        {
            remote_uuid = hs.get_source_uuid();

            URI uri(hs.get_node_address());
            string host = parse_host(uri.get_authority());
            string port = parse_port(uri.get_authority());
            if (host == "")
            {
                uri.set_authority(tp->get_remote_host() + ':' + port);
            }
            remote_addr = uri.to_string();
            GMCastMessage ok(GMCastMessage::P_HANDSHAKE_OK, local_uuid);
            send_msg(ok);
            set_state(S_OK);
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
                throw FatalException("nil uuid or empty address");
            nl.push_back(GMCastNode(true, i->first, i->second));
        }
        GMCastMessage msg(GMCastMessage::P_TOPOLOGY_CHANGE, local_uuid, group_name, nl);
        
        send_msg(msg);

    }


    void handle_user(const GMCastMessage& hs) 
    {

        if (get_state() != S_OK)
            throw RuntimeException("handle_user(): invalid state");
        if (hs.get_type() < GMCastMessage::P_USER_BASE)
            throw RuntimeException("handle_user(): invalid user message");
        throw FatalException("");
    }
    
    void handle_message(const GMCastMessage& msg) 
    {


        LOG_DEBUG(string("message type: ") + make_int(msg.get_type()).to_string());

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
    if (uri.get_scheme() != Conf::TcpScheme)
    {
        return false;
    }
    return true;
}


GMCast::GMCast(const URI& uri, EventLoop* event_loop, Monitor* mon_) :
    Transport(uri, event_loop, mon_),
    my_uuid(0, 0),
    proto_map(),
    spanning_tree(),
    listener(0),
    listen_addr(),
    initial_addr(),
    pending_addrs(),
    remote_addrs(),
    group_name()
{
    
    if (uri.get_scheme() != Conf::GMCastScheme)
    {
        LOG_FATAL("invalid uri scheme: " + uri.get_scheme());
        throw FatalException("invalid uri scheme");
    }
    
    URIQueryList::const_iterator i = uri.get_query_list().find(Conf::GMCastQueryListenAddr);
    if (i == uri.get_query_list().end())
    {
        listen_addr = Conf::TcpScheme + "://:4567";
        log_info << "set default listen addr to: " << listen_addr;
    }
    else
    {
        listen_addr = get_query_value(i);
    }
    if (check_uri(listen_addr) == false)
    {
        log_fatal << "Listen addr uri '" << listen_addr << "' is not valid";
        throw FatalException("invalid uri");
    }
    
    if (uri.get_authority() != "")
    {
        initial_addr = Conf::TcpScheme + "://" + uri.get_authority();
    }
    
    i = uri.get_query_list().find(Conf::GMCastQueryGroup);
    if (i == uri.get_query_list().end())
    {
        LOG_FATAL("group not defined in uri: " + uri.to_string());
        throw FatalException("group not defined");
    }
    group_name = i->second;
    fd = PseudoFd::alloc_fd();
}



GMCast::~GMCast()
{
    if (listener != 0)
    {
        stop();
    }
    PseudoFd::release_fd(fd);
}




void GMCast::start() 
{
    
    URI listen_uri(listen_addr);
    set_tcp_params(&listen_uri);
    
    listener = Transport::create(listen_uri, event_loop);
    listener->listen();
    listener->set_up_context(this, listener->get_fd());
    LOG_DEBUG(string("Listener: ") + make_int(listener->get_fd()).to_string());
    if (initial_addr != "")
    {
        insert_address(initial_addr, UUID(), pending_addrs);
        gmcast_connect(initial_addr);
    }
    event_loop->insert(fd, this);
    event_loop->queue_event(fd, Event(Event::E_USER, Time::now() + Time(0, 500000)));
}


void GMCast::stop() 
{
    event_loop->erase(fd);
    
    listener->close();
    delete listener;
    listener = 0;
    
    spanning_tree.clear();
    for (ProtoMap::iterator i = proto_map.begin(); i != proto_map.end(); ++i)
    {
        Transport* tp = get_gmcast_proto(i)->get_transport();
        tp->close();
        delete tp;
        delete get_gmcast_proto(i);
    }
    proto_map.clear();
    pending_addrs.clear();
    remote_addrs.clear();
}


void GMCast::gmcast_accept() 
{
    Transport* tp = listener->accept();
    tp->set_up_context(this, tp->get_fd());
    pair<ProtoMap::iterator, bool> ret = 
        proto_map.insert(
            make_pair(tp->get_fd(), 
                      new GMCastProto(tp, listen_addr, string(""), 
                                      get_uuid(), group_name)));
    if (ret.second == false)
        throw FatalException("");
    ret.first->second->send_handshake();
}

void GMCast::gmcast_connect(const string& addr) 
{
    if (addr == listen_addr)
    {
        return;
    }
    URI connect_uri(addr);
    set_tcp_params(&connect_uri);
    Transport* tp = Transport::create(connect_uri, event_loop);
    try 
    {
        tp->connect();
    }
    catch (RuntimeException e)
    {
        LOG_WARN(string("connect failed: ") + e.what());
        delete tp;
        return;
    }
            
    tp->set_up_context(this, tp->get_fd());
    std::pair<ProtoMap::iterator, bool> ret = 
        proto_map.insert(
            make_pair(tp->get_fd(), new GMCastProto(tp, listen_addr, 
                                                    addr, get_uuid(), group_name)));
    if (ret.second == false)
        throw FatalException("");
    ret.first->second->wait_handshake();
}

void GMCast::gmcast_forget(const UUID& uuid)
{
    /* Close all proto entries corresponding to uuid */
    
    ProtoMap::iterator pi, pi_next;
    for (pi = proto_map.begin(); pi != proto_map.end(); pi = pi_next)
    {
        pi_next = pi, ++pi_next;
        GMCastProto* rp = get_gmcast_proto(pi);
        if (rp->get_remote_uuid() == uuid)
        {
            rp->get_transport()->close();
            event_loop->release_protolay(rp->get_transport());
            delete rp;
            proto_map.erase(pi);
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
    
    if (remote_addrs.find(rp->get_remote_addr()) == remote_addrs.end())
    {
        insert_address(rp->get_remote_addr(), rp->get_remote_uuid(),
                       remote_addrs);
    }
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
            int rsecs = std::min(get_retry_cnt(i)*get_retry_cnt(i), 30);
            Time rtime = Time::now() + Time(rsecs, 0);
            LOG_DEBUG(string("setting next reconnect time to ") + rtime.to_string() + " for " + remote_addr);
            set_next_reconnect(i, rtime);
        }
    }
    
    delete rp;
}

void GMCast::remove_proto(const int fd)
{

    proto_map.erase(fd);
    spanning_tree.erase(fd);
}



bool GMCast::is_connected(const string& addr, const UUID& uuid) const
{
    for (ProtoMap::const_iterator i = proto_map.begin();
         i != proto_map.end(); ++i)
    {
        if (addr == get_gmcast_proto(i)->get_remote_addr() || 
            uuid == get_gmcast_proto(i)->get_remote_uuid())
            return true;
    }
    return false;
}

void GMCast::insert_address(const string& addr, const UUID& uuid, 
                            AddrList& alist)
{
    if (addr == listen_addr)
    {
        throw std::logic_error("trying to add self to addr list");
    }
    
    if (alist.insert(make_pair(addr, 
                               Timing(Time::now(), Time::now(), uuid))).second == false)
    {
        log_warn << "duplicate entry " << addr;
    }
    else
    {
        log_debug << self_string() << " new address entry "
                  << uuid.to_string() << " "
                  << addr;
    }
}





using namespace boost;
using std::vector;

typedef adjacency_list<listS, vecS, undirectedS, property<vertex_index_t, UUID>, 
                       property<edge_weight_t, int> > Graph;
typedef graph_traits <Graph>::edge_descriptor Edge;
typedef graph_traits <Graph>::vertex_descriptor Vertex;
typedef pair<int, int> E;


static inline int find_safe(const map<const UUID, int>& m, const UUID& val)
{
    map<const UUID, int>::const_iterator i = m.find(val);
    if (i == m.end())
    {
        LOG_FATAL(string("missing UUID ") + val.to_string());
        throw FatalException("");
    }
    return i->second;
}

static inline const UUID& find_safe(const map<const int, UUID>& m, const int val)
{
    map<const int, UUID>::const_iterator i = m.find(val);
    if (i == m.end())
        throw FatalException("");
    return i->second;
}

void GMCast::compute_spanning_tree(const UUIDToAddressMap& uuid_map)
{
    /* Construct mapping between indexing [0, n) and UUIDs, as well as
     * between UUIDs and proto map entries */
    map<const UUID, int> uuid_to_idx;
    map<const int, UUID> idx_to_uuid;
    map<const UUID, pair<const int, GMCastProto*> > uuid_to_proto;
    
    if (uuid_to_idx.insert(pair<const UUID, int>(get_uuid(), 0)).second == false)
        throw FatalException("");
    if (idx_to_uuid.insert(pair<const int, UUID>(0, get_uuid())).second == false)
        throw FatalException("");
    int n = 1;
    for (UUIDToAddressMap::const_iterator i = uuid_map.begin();
         i != uuid_map.end(); ++i)
    {
        if (uuid_to_idx.insert(make_pair(i->first, n)).second == true)
        {
            if (idx_to_uuid.insert(make_pair(n, i->first)).second == false)
                throw FatalException("");
            ++n;
        }
    }
    
    /* Construct lists of edges and weights */
    list<E> edges;
    list<int> weights;
    for (ProtoMap::const_iterator i = proto_map.begin(); i != proto_map.end();
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
    spanning_tree.clear();
    for (ProtoMap::iterator i = proto_map.begin(); i != proto_map.end(); ++i)
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
            
            map<const UUID, pair<const int, GMCastProto*> >::const_iterator up_target = 
                uuid_to_proto.find(find_safe(idx_to_uuid, target(*ei, graph)));
            
            if (up_target != uuid_to_proto.end())
            {
                // std::cerr << up_target->second.first << " ";
                if (spanning_tree.insert(
                        make_pair(up_target->second.first, 
                                  up_target->second.second)).second == false)
                    throw FatalException("");
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
        for (i = spanning_tree.begin(); i != spanning_tree.end(); ++i)
        {
            if (i->second->get_remote_uuid() == source_uuid)
            {
                i->second->set_send_ttl(2);
                break;
            }
        }
        if (i == spanning_tree.end())
        {
            LOG_WARN(string("no outgoing route found for ") + source_uuid.to_string());
        }
    }

    if (st.begin() == st.end())
    {
        log_debug << self_string() << " single hop spanning tree of size " << spanning_tree.size();
    }
    else
    {
        log_debug << self_string() << " multi hop spanning tree of size " << spanning_tree.size();
    }

}





void GMCast::update_addresses()
{
    UUIDToAddressMap uuid_map;

    /* Add all established connections into uuid_map and update 
     * list of remote addresses */
    for (ProtoMap::iterator i = proto_map.begin(); i != proto_map.end(); ++i)
    {
        GMCastProto* rp = get_gmcast_proto(i);
        if (rp->get_state() == GMCastProto::S_OK)
        {
            if (rp->get_remote_addr() == "" || rp->get_remote_uuid() == UUID())
            {
                LOG_ERROR("this: " 
                          + get_uuid().to_string() + " " 
                          + listen_addr 
                          + " remote: "
                          + i->second->get_remote_uuid().to_string() + " "
                          + i->second->get_remote_addr());
                throw FatalException("protocol error");
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
                         << i->second->get_remote_addr();
                insert_address(rp->get_remote_addr(), rp->get_remote_uuid(), 
                               remote_addrs);
            }
        }
    }
    
    /* Send topology change message containing only established 
     * connections */
    for (ProtoMap::iterator i = proto_map.begin(); i != proto_map.end(); ++i)
    {
        GMCastProto* gp = get_gmcast_proto(i);
        if (gp->get_state() == GMCastProto::S_OK)
            gp->send_topology_change(uuid_map);
    }

    /* Add entries reported by all other nodes to get complete view 
     * of existing uuids/addresses */
    for (ProtoMap::iterator i = proto_map.begin(); i != proto_map.end(); ++i)
    {
        GMCastProto* rp = get_gmcast_proto(i);
        if (rp->get_state() == GMCastProto::S_OK)
        {
            for (UUIDToAddressMap::const_iterator 
                     j = rp->get_uuid_map().begin(); 
                 j != rp->get_uuid_map().end(); ++j)
            {
                if (j->second == "" || j->first == UUID())
                {
                    LOG_ERROR("this: " 
                              + get_uuid().to_string() + " " 
                              + listen_addr 
                              + " remote: "
                              + i->second->get_remote_uuid().to_string() + " "
                              + i->second->get_remote_addr() + " "
                              + j->first.to_string() + " "
                              + j->second);
                    throw FatalException("protocol error");
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
                    set_retry_cnt(pending_addrs.find(j->second), max_retry_cnt - 3);
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
            throw std::logic_error("own uuid in addr list");
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
                    log_info << self_string() << " reconnecting " 
                             << get_uuid(i).to_string() 
                             << " " << get_address(i);
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
        else
        {
            set_retry_cnt(i, 0);
        }
    }
}

void GMCast::handle_event(const int fd, const Event& pe) 
{
    Critical crit(mon);
    LOG_DEBUG("handle event");

    update_addresses();
    reconnect();
    
    event_loop->queue_event(fd, Event(Event::E_USER, 
                                    Time::now() + Time(0, 500000)));
}


void GMCast::forward_message(const int cid, const ReadBuf* rb, 
                             const size_t offset, const GMCastMessage& msg)
{

    WriteBuf wb(rb->get_buf(offset), rb->get_len(offset));
    byte_t buf[20];
    size_t hdrlen;
    if ((hdrlen = msg.write(buf, sizeof(buf), 0)) == 0)
        throw FatalException("");
    wb.prepend_hdr(buf, hdrlen);
    for (ProtoMap::iterator i = spanning_tree.begin(); i != spanning_tree.end();
         ++i)
    {
        if (i->first != cid)
        {
            LOG_DEBUG(string("forwarding message ") + msg.get_source_uuid().to_string() + " -> " + i->second->get_remote_uuid().to_string());
            i->second->get_transport()->handle_down(&wb, 0);
        }
    }

}

void GMCast::handle_up(const int cid, const ReadBuf* rb, 
                      const size_t offset, const ProtoUpMeta* um) 
{
    Critical crit(mon);
    if (listener == 0)
    {
        LOG_WARN("");
        return;
    }
    if (cid == listener->get_fd()) 
    {
        gmcast_accept();
    } 
    else 
    {
        GMCastMessage msg;
        size_t off;
        if (rb != 0)
        {
            if ((off = msg.read(rb->get_buf(), rb->get_len(), offset)) == 0)
                throw RuntimeException("message unserialization");
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
        
        
        bool changed = false;
        ProtoMap::iterator i = proto_map.find(cid);
        if (i == proto_map.end()) 
        {
            
            LOG_WARN(string("unknown fd ") + make_int(cid).to_string());
            return;
        }
        
        GMCastProto* rp = get_gmcast_proto(i);
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
        reconnect();
    }
}

int GMCast::handle_down(WriteBuf* wb, const ProtoDownMeta* dm) 
{
    Critical crit(mon);
    
    for (ProtoMap::iterator i = spanning_tree.begin();
         i != spanning_tree.end(); ++i)
    {
        GMCastProto* rp = get_gmcast_proto(i);
        GMCastMessage msg(GMCastMessage::P_USER_BASE, get_uuid(), rp->get_send_ttl());
        byte_t hdrbuf[20];
        size_t wlen;
        if ((wlen = msg.write(hdrbuf, sizeof(hdrbuf), 0)) == 0)
            throw FatalException("short buffer");
        if (msg.get_ttl() > 1)
        {
            LOG_DEBUG(string("msg ttl: ") + make_int(msg.get_ttl()).to_string());
        }
        wb->prepend_hdr(hdrbuf, wlen);
        int err;
        
        if ((err = rp->get_transport()->handle_down(wb, 0)) != 0)
        {
            LOG_WARN(string("transport: ") + ::strerror(err));
        }
        wb->rollback_hdr(wlen);
    }
    
    return 0;
}

END_GCOMM_NAMESPACE
