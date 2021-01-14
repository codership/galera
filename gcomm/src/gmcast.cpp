/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#include "gmcast.hpp"
#include "gmcast_proto.hpp"

#include "gcomm/common.hpp"
#include "gcomm/conf.hpp"
#include "gcomm/util.hpp"
#include "gcomm/map.hpp"
#include "defaults.hpp"
#include "gu_convert.hpp"
#include "gu_resolver.hpp"
#include "gu_asio.hpp" // gu::conf::use_ssl

using namespace std::rel_ops;

using gcomm::gmcast::Proto;
using gcomm::gmcast::ProtoMap;
using gcomm::gmcast::Link;
using gcomm::gmcast::LinkMap;
using gcomm::gmcast::Message;

const long gcomm::GMCast::max_retry_cnt_(std::numeric_limits<int>::max());

static void set_tcp_defaults (gu::URI* uri)
{
    // what happens if there is already this parameter?
    uri->set_option(gcomm::Conf::TcpNonBlocking, gu::to_string(1));
}


static bool check_tcp_uri(const gu::URI& uri)
{
    return (uri.get_scheme() == gu::scheme::tcp ||
            uri.get_scheme() == gu::scheme::ssl);
}

static std::string get_scheme(bool use_ssl)
{
    if (use_ssl == true)
    {
        return gu::scheme::ssl;
    }
    return gu::scheme::tcp;
}

//
// Check if the node should stay isolated.
// Possible outcomes:
// * Return false, node should continue reconnecting and accepting connections
//   (isolate = 0)
// * Return true, node should remain isolated (isolate = 1)
// * Throw fatal exception to terminate the backend (isolate = 2)
//
static inline bool is_isolated(int isolate)
{
    switch (isolate)
    {
    case 1:
        return true;
    case 2:
        gu_throw_fatal<< "Gcomm backend termination was "
                      << "requested by setting gmcast.isolate=2.";
        break;
    default:
        break;
    }
    return false;
}

gcomm::GMCast::GMCast(Protonet& net, const gu::URI& uri,
                      const UUID* my_uuid)
    :
    Transport     (net, uri),
    version_(check_range(Conf::GMCastVersion,
                         param<int>(conf_, uri, Conf::GMCastVersion, "0"),
                         0, max_version_ + 1)),
    segment_ (check_range(Conf::GMCastSegment,
                          param<int>(conf_, uri, Conf::GMCastSegment, "0"),
                          0, 255)),
    my_uuid_      (my_uuid ? *my_uuid : UUID(0, 0)),
#ifdef GALERA_HAVE_SSL
    use_ssl_      (param<bool>(conf_, uri, gu::conf::use_ssl, "false")),
#else
    use_ssl_(),
#endif // GALERA_HAVE_SSL
    // @todo: technically group name should be in path component
    group_name_   (param<std::string>(conf_, uri, Conf::GMCastGroup, "")),
    listen_addr_  (
        param<std::string>(
            conf_, uri, Conf::GMCastListenAddr,
            get_scheme(use_ssl_) + "://0.0.0.0")), // how to make it IPv6 safe?
    initial_addrs_(),
    mcast_addr_   (param<std::string>(conf_, uri, Conf::GMCastMCastAddr, "")),
    bind_ip_      (""),
    mcast_ttl_    (check_range(
                       Conf::GMCastMCastTTL,
                       param<int>(conf_, uri, Conf::GMCastMCastTTL, "1"),
                       1, 256)),
    listener_     (),
    mcast_        (),
    pending_addrs_(),
    remote_addrs_ (),
    addr_blacklist_(),
    relaying_     (false),
    isolate_      (0),
    prim_view_reached_(false),
    proto_map_    (new ProtoMap()),
    relay_set_    (),
    segment_map_  (),
    self_index_   (std::numeric_limits<size_t>::max()),
    time_wait_    (param<gu::datetime::Period>(
                       conf_, uri,
                       Conf::GMCastTimeWait, Defaults::GMCastTimeWait)),
    check_period_ ("PT0.5S"),
    peer_timeout_ (param<gu::datetime::Period>(
                       conf_, uri,
                       Conf::GMCastPeerTimeout, Defaults::GMCastPeerTimeout)),
    max_initial_reconnect_attempts_(
        param<int>(conf_, uri,
                   Conf::GMCastMaxInitialReconnectAttempts,
                   gu::to_string(max_retry_cnt_))),
    next_check_   (gu::datetime::Date::monotonic())
{
    log_info << "GMCast version " << version_;

    if (group_name_ == "")
    {
        gu_throw_error (EINVAL) << "Group not defined in URL: "
                                << uri_.to_string();
    }

    set_initial_addr(uri_);

    try
    {
        listen_addr_ = uri_.get_option (Conf::GMCastListenAddr);
    }
    catch (gu::NotFound&) {}

    try
    {
        gu::URI uri(listen_addr_); /* check validity of the address */
    }
    catch (gu::Exception&)
    {
        /* most probably no scheme, try to append one and see if it succeeds */
        listen_addr_ = uri_string(get_scheme(use_ssl_), listen_addr_);
        gu_trace(gu::URI uri(listen_addr_));
    }

    gu::URI listen_uri(listen_addr_);

    if (check_tcp_uri(listen_uri) == false)
    {
        gu_throw_error (EINVAL) << "listen addr '" << listen_addr_
                                << "' does not specify supported protocol";
    }

    if (gu::net::resolve(listen_uri).get_addr().is_anyaddr() == false)
    {
        // bind outgoing connections to the same address as listening.
        gu_trace(bind_ip_ = listen_uri.get_host());
    }

    std::string port(Defaults::GMCastTcpPort);

    try
    {
        port = listen_uri.get_port();
    }
    catch (gu::NotSet&)
    {
        // if no listen port is set for listen address in the options,
        // see if base port was configured
        try
        {
            port = conf_.get(BASE_PORT_KEY);
        }
        catch (gu::NotSet&)
        {
            // if no base port configured, try port from the connection address
            try { port = uri_.get_port(); } catch (gu::NotSet&) {}
        }

        listen_addr_ += ":" + port;
    }

    conf_.set(BASE_PORT_KEY, port);

    listen_addr_ = gu::net::resolve(listen_addr_).to_string();
    // resolving sets scheme to tcp, have to rewrite for ssl
    if (use_ssl_ == true)
    {
        listen_addr_.replace(0, 3, gu::scheme::ssl);
    }
    std::set<std::string>::iterator iaself(initial_addrs_.find(listen_addr_));
    if (iaself != initial_addrs_.end())
    {
        log_debug << "removing own listen address '" << *iaself
                  << "' from initial address list";
        initial_addrs_.erase(iaself);
    }

    if (mcast_addr_ != "")
    {
        try
        {
            port = param<std::string>(conf_, uri_, Conf::GMCastMCastPort, port);
        }
        catch (gu::NotFound&) {}

        mcast_addr_ = gu::net::resolve(
            uri_string(gu::scheme::udp, mcast_addr_, port)).to_string();
    }

    log_info << self_string() << " listening at " << listen_addr_;
    log_info << self_string() << " multicast: " << mcast_addr_
             << ", ttl: " << mcast_ttl_;

    conf_.set(Conf::GMCastListenAddr, listen_addr_);
    conf_.set(Conf::GMCastMCastAddr, mcast_addr_);
    conf_.set(Conf::GMCastVersion, gu::to_string(version_));
    conf_.set(Conf::GMCastTimeWait, gu::to_string(time_wait_));
    conf_.set(Conf::GMCastMCastTTL, gu::to_string(mcast_ttl_));
    conf_.set(Conf::GMCastPeerTimeout, gu::to_string(peer_timeout_));
    conf_.set(Conf::GMCastSegment, gu::to_string<int>(segment_));
}

gcomm::GMCast::~GMCast()
{
    if (listener_ != 0) close();

    delete proto_map_;
}

void gcomm::GMCast::set_initial_addr(const gu::URI& uri)
{

    const gu::URI::AuthorityList& al(uri.get_authority_list());

    for (gu::URI::AuthorityList::const_iterator i(al.begin());
         i != al.end(); ++i)
    {
        std::string host;
        try
        {
            host = i->host();
        }
        catch (gu::NotSet& ns)
        {
            gu_throw_error(EINVAL) << "Unset host in URL " << uri;
        }

        if (host_is_any(host)) continue;

        std::string port;
        try
        {
            port = i->port();
        }
        catch (gu::NotSet&)
        {
            try
            {
                port = conf_.get(BASE_PORT_KEY);
            }
            catch (gu::NotFound&)
            {
                port = Defaults::GMCastTcpPort;
            }
            catch (gu::NotSet&)
            {
                port = Defaults::GMCastTcpPort;
            }
        }

        std::string initial_uri = uri_string(get_scheme(use_ssl_), host, port);
        std::string initial_addr;
        try
        {
            initial_addr = gu::net::resolve(initial_uri).to_string();
        }
        catch (gu::Exception& )
        {
            log_warn << "Failed to resolve " << initial_uri;
            continue;
        }
        // resolving sets scheme to tcp, have to rewrite for ssl
        if (use_ssl_ == true)
        {
            initial_addr.replace(0, 3, gu::scheme::ssl);
        }

        if (check_tcp_uri(initial_addr) == false)
        {
            gu_throw_error (EINVAL) << "initial addr '" << initial_addr
                                    << "' is not valid";
        }

        log_debug << self_string() << " initial addr: " << initial_addr;
        initial_addrs_.insert(initial_addr);

    }

}

void gcomm::GMCast::connect_precheck(bool start_prim)
{
    if (!start_prim && initial_addrs_.empty()) {
        gu_throw_fatal << "No address to connect";
    }
}

void gcomm::GMCast::connect()
{
    pstack_.push_proto(this);
    log_debug << "gmcast " << uuid() << " connect";

    gu::URI listen_uri(listen_addr_);

    set_tcp_defaults (&listen_uri);

    listener_ = pnet().acceptor(listen_uri);
    gu_trace (listener_->listen(listen_uri));

    if (!mcast_addr_.empty())
    {
        gu::URI mcast_uri(
            mcast_addr_ + '?'
            + gcomm::Socket::OptIfAddr + '='
            + gu::URI(listen_addr_).get_host()+'&'
            + gcomm::Socket::OptNonBlocking + "=1&"
            + gcomm::Socket::OptMcastTTL    + '=' + gu::to_string(mcast_ttl_)
            );

        mcast_ = pnet().socket(mcast_uri);
        gu_trace(mcast_->connect(mcast_uri));
    }

    if (!initial_addrs_.empty())
    {
        for (std::set<std::string>::const_iterator i(initial_addrs_.begin());
             i != initial_addrs_.end(); ++i)
        {
            insert_address(*i, UUID(), pending_addrs_);
            AddrList::iterator ai(pending_addrs_.find(*i));
            AddrList::value(ai).set_max_retries(max_retry_cnt_);
            gu_trace (gmcast_connect(*i));
        }
    }
}


void gcomm::GMCast::connect(const gu::URI& uri)
{
    set_initial_addr(uri);
    connect();
}



void gcomm::GMCast::close(bool force)
{
    log_debug << "gmcast " << uuid() << " close";
    pstack_.pop_proto(this);
    if (mcast_)
    {
        mcast_->close();
        // delete mcast;
        // mcast = 0;
    }

    gcomm_assert(listener_ != 0);
    listener_->close();
    listener_.reset();

    segment_map_.clear();
    for (ProtoMap::iterator
             i = proto_map_->begin(); i != proto_map_->end(); ++i)
    {
        delete ProtoMap::value(i);
    }

    proto_map_->clear();
    pending_addrs_.clear();
    remote_addrs_.clear();
    prim_view_reached_ = false;
}

//
// Private
//


// Find other local endpoint matching to proto
static const Proto*
find_other_local_endpoint(const gcomm::gmcast::ProtoMap& proto_map,
                          const gcomm::gmcast::Proto* proto)
{
    for (gcomm::gmcast::ProtoMap::const_iterator i(proto_map.begin());
         i != proto_map.end(); ++i)
    {
        if (i->second != proto &&
            i->second->handshake_uuid() == proto->handshake_uuid())
        {
            return i->second;
        }
    }
    return 0;
}

// Find other endpoint with same remote UUID
static const Proto*
find_other_endpoint_same_remote_uuid(const gcomm::gmcast::ProtoMap& proto_map,
                                     const gcomm::gmcast::Proto* proto)
{
    for (gcomm::gmcast::ProtoMap::const_iterator i(proto_map.begin());
         i != proto_map.end(); ++i)
    {
        if (i->second != proto &&
            i->second->remote_uuid() == proto->remote_uuid())
        {
            return i->second;
        }
    }
    return 0;
}

bool gcomm::GMCast::is_own(const gmcast::Proto* proto) const
{
    assert(proto->remote_uuid() != gcomm::UUID::nil());
    if (proto->remote_uuid() != uuid())
    {
        return false;
    }
    return find_other_local_endpoint(*proto_map_, proto);
}

void gcomm::GMCast::blacklist(const gmcast::Proto* proto)
{
    initial_addrs_.erase(proto->remote_addr());
    pending_addrs_.erase(proto->remote_addr());
    addr_blacklist_.insert(std::make_pair(
                               proto->remote_addr(),
                               AddrEntry(gu::datetime::Date::monotonic(),
                                         gu::datetime::Date::monotonic(),
                                         proto->remote_uuid())));
}

bool gcomm::GMCast::is_not_own_and_duplicate_exists(
    const Proto* proto) const
{
    assert(proto->remote_uuid() != gcomm::UUID::nil());
    const Proto* other(find_other_local_endpoint(*proto_map_, proto));
    if (!other)
    {
        // Not own
        // Check if remote UUID matches to self
        if (proto->remote_uuid() == uuid())
        {
            return true;
        }
        // Check if other proto entry with same remote
        // UUID but different remote address exists.
        other = find_other_endpoint_same_remote_uuid(*proto_map_, proto);
        if (other && other->remote_addr() != proto->remote_addr())
        {
            return true;
        }
    }
    return false;
}

// Erase proto entry in safe manner
// 1) Erase from relay_set_
// 2) Erase from proto_map_
// 3) Delete proto entry
void gcomm::GMCast::erase_proto(gmcast::ProtoMap::iterator i)
{
    Proto* p(ProtoMap::value(i));
    RelayEntry e(p, p->socket().get());
    RelaySet::iterator si(relay_set_.find(e));
    if (si != relay_set_.end())
    {
        relay_set_.erase(si);
    }
    proto_map_->erase(i);
    delete p;
}

void gcomm::GMCast::gmcast_accept()
{
    SocketPtr tp;

    try
    {
        tp = listener_->accept();
    }
    catch (gu::Exception& e)
    {
        log_warn << e.what();
        return;
    }

    if (is_isolated(isolate_))
    {
        log_debug << "dropping accepted socket due to isolation";
        tp->close();
        return;
    }

    Proto* peer = new Proto (
        *this,
        version_,
        tp,
        listener_->listen_addr(),
        "",
        mcast_addr_,
        segment_,
        group_name_);
    std::pair<ProtoMap::iterator, bool> ret =
        proto_map_->insert(std::make_pair(tp->id(), peer));

    if (ret.second == false)
    {
        delete peer;
        gu_throw_fatal << "Failed to add peer to map";
    }
    if (tp->state() == Socket::S_CONNECTED)
    {
        peer->send_handshake();
    }
    else
    {
        log_debug << "accepted socket is connecting";
    }
    log_debug << "handshake sent";
}


void gcomm::GMCast::gmcast_connect(const std::string& remote_addr)
{
    if (remote_addr == listen_addr_) return;

    gu::URI connect_uri(remote_addr);

    set_tcp_defaults (&connect_uri);

    if (!bind_ip_.empty())
    {
        connect_uri.set_option(gcomm::Socket::OptIfAddr, bind_ip_);
    }

    SocketPtr tp = pnet().socket(connect_uri);

    try
    {
        tp->connect(connect_uri);
    }
    catch (gu::Exception& e)
    {
        log_debug << "Connect failed: " << e.what();
        // delete tp;
        return;
    }

    Proto* peer = new Proto (
        *this,
        version_,
        tp,
        listener_->listen_addr(),
        remote_addr,
        mcast_addr_,
        segment_,
        group_name_);

    std::pair<ProtoMap::iterator, bool> ret =
        proto_map_->insert(std::make_pair(tp->id(), peer));

    if (ret.second == false)
    {
        delete peer;
        gu_throw_fatal << "Failed to add peer to map";
    }

    ret.first->second->wait_handshake();
}


void gcomm::GMCast::gmcast_forget(const UUID& uuid,
                                  const gu::datetime::Period& wait_period)
{
    /* Close all proto entries corresponding to uuid */

    ProtoMap::iterator pi, pi_next;
    for (pi = proto_map_->begin(); pi != proto_map_->end(); pi = pi_next)
    {
        pi_next = pi, ++pi_next;
        Proto* rp = ProtoMap::value(pi);
        if (rp->remote_uuid() == uuid)
        {
            erase_proto(pi);
        }
    }

    /* Set all corresponding entries in address list to have retry cnt
     * greater than max retries and next reconnect time after some period */
    AddrList::iterator ai;
    for (ai = remote_addrs_.begin(); ai != remote_addrs_.end(); ++ai)
    {
        AddrEntry& ae(AddrList::value(ai));
        if (ae.uuid() == uuid)
        {
            log_info << "forgetting " << uuid
                     << " (" << AddrList::key(ai) << ")";

            ProtoMap::iterator pi, pi_next;
            for (pi = proto_map_->begin(); pi != proto_map_->end(); pi = pi_next)
            {
                pi_next = pi, ++pi_next;
                if (ProtoMap::value(pi)->remote_addr() == AddrList::key(ai))
                {
                    log_info << "deleting entry " << AddrList::key(ai);
                    erase_proto(pi);
                }
            }
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            gu::datetime::Date now(gu::datetime::Date::monotonic());
            // Don't reduce next reconnect time if it is set greater than
            // requested
            if ((now + wait_period > ae.next_reconnect()) ||
                (ae.next_reconnect() == gu::datetime::Date::max()))
            {
                ae.set_next_reconnect(gu::datetime::Date::monotonic() + wait_period);
            }
            else
            {
                log_debug << "not decreasing next reconnect for " << uuid;
            }
        }
    }

    /* Update state */
    update_addresses();
}

void gcomm::GMCast::handle_connected(Proto* rp)
{
    const SocketPtr tp(rp->socket());
    assert(tp->state() == Socket::S_CONNECTED);
    log_debug << "transport " << tp << " connected";
    if (rp->state() == Proto::S_INIT)
    {
        log_debug << "sending handshake";
        // accepted socket was waiting for underlying transport
        // handshake to finish
        rp->send_handshake();
    }
}

void gcomm::GMCast::handle_established(Proto* est)
{
    log_info << self_string() << " connection established to "
             << est->remote_uuid() << " "
             << est->remote_addr();
    // UUID checks are handled during protocol handshake
    assert(est->remote_uuid() != uuid());

    if (is_evicted(est->remote_uuid()))
    {
        log_warn << "Closing connection to evicted node " << est->remote_uuid();
        erase_proto(proto_map_->find_checked(est->socket()->id()));
        update_addresses();
        return;
    }

    // If address is found from pending_addrs_, move it to remote_addrs list
    // and set retry cnt to -1
    const std::string& remote_addr(est->remote_addr());
    AddrList::iterator i(pending_addrs_.find(remote_addr));

    if (i != pending_addrs_.end())
    {
        log_debug << "Erasing " << remote_addr << " from panding list";
        pending_addrs_.erase(i);
    }

    if ((i = remote_addrs_.find(remote_addr)) == remote_addrs_.end())
    {
        log_debug << "Inserting " << remote_addr << " to remote list";

        insert_address (remote_addr, est->remote_uuid(), remote_addrs_);
        i = remote_addrs_.find(remote_addr);
    }
    else if (AddrList::value(i).uuid() != est->remote_uuid())
    {
        log_info << "remote endpoint " << est->remote_addr()
                 << " changed identity " << AddrList::value(i).uuid().full_str()
                 << " -> " << est->remote_uuid().full_str();
        remote_addrs_.erase(i);
        i = remote_addrs_.insert_unique(
            make_pair(est->remote_addr(),
                      AddrEntry(gu::datetime::Date::monotonic(),
                                gu::datetime::Date::monotonic(),
                                est->remote_uuid())));
    }

    if (AddrList::value(i).retry_cnt() >
        AddrList::value(i).max_retries())
    {
        log_warn << "discarding established (time wait) "
                 << est->remote_uuid()
                 << " (" << est->remote_addr() << ") ";
        erase_proto(proto_map_->find(est->socket()->id()));
        update_addresses();
        return;
    }

    // send_up(Datagram(), p->remote_uuid());

    // init retry cnt to -1 to avoid unnecessary logging at first attempt
    // max retries will be readjusted in handle stable view
    AddrList::value(i).set_retry_cnt(-1);
    AddrList::value(i).set_max_retries(max_initial_reconnect_attempts_);

    // Cleanup all previously established entries with same
    // remote uuid. It is assumed that the most recent connection
    // is usually the healthiest one.
    ProtoMap::iterator j, j_next;
    for (j = proto_map_->begin(); j != proto_map_->end(); j = j_next)
    {
        j_next = j, ++j_next;

        Proto* p(ProtoMap::value(j));

        if (p->remote_uuid() == est->remote_uuid())
        {
            if (p->handshake_uuid() < est->handshake_uuid())
            {
                log_debug << self_string()
                          << " cleaning up duplicate "
                          << p->socket()
                          << " after established "
                          << est->socket();
                erase_proto(j);
            }
            else if (p->handshake_uuid() > est->handshake_uuid())
            {
                log_debug << self_string()
                          << " cleaning up established "
                          << est->socket()
                          << " which is duplicate of "
                          << p->socket();
                erase_proto(proto_map_->find_checked(est->socket()->id()));
                update_addresses();
                return;
            }
            else
            {
                assert(p == est);
            }
        }
    }

    AddrList::iterator ali(find_if(remote_addrs_.begin(),
                                   remote_addrs_.end(),
                                   AddrListUUIDCmp(est->remote_uuid())));
    if (ali != remote_addrs_.end())
    {
        AddrList::value(ali).set_last_connect();
    }
    else
    {
        log_warn << "peer " << est->remote_addr()
                 << " not found from remote addresses";
    }

    update_addresses();
}

void gcomm::GMCast::handle_failed(Proto* failed)
{
    log_debug << "handle failed: " << *failed;
    const std::string& remote_addr = failed->remote_addr();

    bool found_ok(false);
    for (ProtoMap::const_iterator i = proto_map_->begin();
         i != proto_map_->end(); ++i)
    {
        Proto* p(ProtoMap::value(i));
        if (p                    != failed      &&
            p->state()       <= Proto::S_OK &&
            p->remote_addr() == failed->remote_addr())
        {
            log_debug << "found live " << *p;
            found_ok = true;
            break;
        }
    }

    if (found_ok == false && remote_addr != "")
    {
        AddrList::iterator i;

        if ((i = pending_addrs_.find(remote_addr)) != pending_addrs_.end() ||
            (i = remote_addrs_.find(remote_addr))  != remote_addrs_.end())
        {
            AddrEntry& ae(AddrList::value(i));
            ae.set_retry_cnt(ae.retry_cnt() + 1);

            gu::datetime::Date rtime = gu::datetime::Date::monotonic() + gu::datetime::Period("PT1S");
            log_debug << self_string()
                      << " setting next reconnect time to "
                      << rtime << " for " << remote_addr;
            ae.set_next_reconnect(rtime);
        }
    }

    erase_proto(proto_map_->find_checked(failed->socket()->id()));
    update_addresses();
}

bool gcomm::GMCast::is_connected(const std::string& addr, const UUID& uuid) const
{
    for (ProtoMap::const_iterator i = proto_map_->begin();
         i != proto_map_->end(); ++i)
    {
        Proto* conn = ProtoMap::value(i);

        if (addr == conn->remote_addr() ||
            uuid == conn->remote_uuid())
        {
            return true;
        }
    }

    return false;
}


void gcomm::GMCast::insert_address (const std::string& addr,
                             const UUID&   uuid,
                             AddrList&     alist)
{
    if (addr == listen_addr_)
    {
        gu_throw_fatal << "Trying to add self addr " << addr << " to addr list";
    }

    if (alist.insert(make_pair(addr,
                               AddrEntry(gu::datetime::Date::monotonic(),
                                         gu::datetime::Date::monotonic(), uuid))).second == false)
    {
        log_warn << "Duplicate entry: " << addr;
    }
    else
    {
        log_debug << self_string() << ": new address entry " << uuid << ' '
                  << addr;
    }
}


void gcomm::GMCast::update_addresses()
{
    LinkMap link_map;
    std::set<UUID> uuids;
    /* Add all established connections into uuid_map and update
     * list of remote addresses */

    ProtoMap::iterator i, i_next;
    for (i = proto_map_->begin(); i != proto_map_->end(); i = i_next)
    {
        i_next = i, ++i_next;

        Proto* rp = ProtoMap::value(i);

        if (rp->state() == Proto::S_OK)
        {
            if (rp->remote_addr() == "" ||
                rp->remote_uuid() == UUID::nil())
            {
                gu_throw_fatal << "Protocol error: local: (" << my_uuid_
                               << ", '" << listen_addr_
                               << "'), remote: (" << rp->remote_uuid()
                               << ", '" << rp->remote_addr() << "')";
            }

            if (remote_addrs_.find(rp->remote_addr()) == remote_addrs_.end())
            {
                log_warn << "Connection exists but no addr on addr list for "
                         << rp->remote_addr();
                insert_address(rp->remote_addr(), rp->remote_uuid(),
                               remote_addrs_);
            }

            if (uuids.insert(rp->remote_uuid()).second == false)
            {
                // Duplicate entry, drop this one
                // @todo Deeper inspection about the connection states
                log_debug << self_string() << " dropping duplicate entry";
                erase_proto(i);
            }
            else
            {
                link_map.insert(Link(rp->remote_uuid(),
                                     rp->remote_addr(),
                                     rp->mcast_addr()));
            }
        }
    }

    /* Send topology change message containing only established
     * connections */
    for (ProtoMap::iterator i = proto_map_->begin(); i != proto_map_->end(); ++i)
    {
        Proto* gp = ProtoMap::value(i);

        // @todo: a lot of stuff here is done for each connection, including
        //        message creation and serialization. Need a mcast_msg() call
        //        and move this loop in there.
        if (gp->state() == Proto::S_OK)
            gp->send_topology_change(link_map);
    }

    /* Add entries reported by all other nodes to address list to
     * get complete view of existing uuids/addresses */
    for (ProtoMap::iterator i = proto_map_->begin(); i != proto_map_->end(); ++i)
    {
        Proto* rp = ProtoMap::value(i);

        if (rp->state() == Proto::S_OK)
        {
            for (LinkMap::const_iterator j = rp->link_map().begin();
                 j != rp->link_map().end(); ++j)
            {
                const UUID& link_uuid(LinkMap::key(j));
                const std::string& link_addr(LinkMap::value(j).addr());
                gcomm_assert(link_uuid != UUID::nil() && link_addr != "");

                if (addr_blacklist_.find(link_addr) != addr_blacklist_.end())
                {
                    log_debug << self_string()
                              << " address '" << link_addr
                              << "' pointing to uuid " << link_uuid
                              << " is blacklisted, skipping";
                    continue;
                }

                if (link_uuid                     != uuid()         &&
                    remote_addrs_.find(link_addr)  == remote_addrs_.end() &&
                    pending_addrs_.find(link_addr) == pending_addrs_.end())
                {
                    log_debug << self_string()
                              << " conn refers to but no addr in addr list for "
                              << link_addr;
                    insert_address(link_addr, link_uuid, remote_addrs_);

                    AddrList::iterator pi(remote_addrs_.find(link_addr));

                    assert(pi != remote_addrs_.end());

                    AddrEntry& ae(AddrList::value(pi));

                    // init retry cnt to -1 to avoid unnecessary logging
                    // at first attempt
                    // max retries will be readjusted in handle stable view
                    ae.set_retry_cnt(-1);
                    ae.set_max_retries(max_initial_reconnect_attempts_);

                    // Add some randomness for first reconnect to avoid
                    // simultaneous connects
                    gu::datetime::Date rtime(gu::datetime::Date::monotonic());

                    rtime = rtime + ::rand() % (100*gu::datetime::MSec);
                    ae.set_next_reconnect(rtime);
                    next_check_ = std::min(next_check_, rtime);
                }
            }
        }
    }

    // Build multicast tree
    log_debug << self_string() << " --- mcast tree begin ---";
    segment_map_.clear();

    Segment& local_segment(segment_map_[segment_]);

    if (mcast_)
    {
        log_debug << mcast_addr_;
        local_segment.push_back(RelayEntry(0, mcast_.get()));
    }

    self_index_ = 0;
    for (ProtoMap::const_iterator i(proto_map_->begin()); i != proto_map_->end();
         ++i)
    {
        Proto* p(ProtoMap::value(i));

        log_debug << "Proto: " << *p;

        if (p->remote_segment() == segment_)
        {
            if (p->state() == Proto::S_OK &&
                (p->mcast_addr() == "" ||
                 p->mcast_addr() != mcast_addr_))
            {
                local_segment.push_back(RelayEntry(p, p->socket().get()));
                if (p->remote_uuid() < uuid())
                {
                    ++self_index_;
                }
            }
        }
        else
        {
            if (p->state() == Proto::S_OK)
            {
                Segment& remote_segment(segment_map_[p->remote_segment()]);
                remote_segment.push_back(RelayEntry(p, p->socket().get()));
            }
        }
    }
    log_debug << self_string() << " self index: " << self_index_;
    log_debug << self_string() << " --- mcast tree end ---";
}


void gcomm::GMCast::reconnect()
{
    if (is_isolated(isolate_))
    {
        log_debug << "skipping reconnect due to isolation";
        return;
    }

    /* Loop over known remote addresses and connect if proto entry
     * does not exist */
    gu::datetime::Date now = gu::datetime::Date::monotonic();
    AddrList::iterator i, i_next;

    for (i = pending_addrs_.begin(); i != pending_addrs_.end(); i = i_next)
    {
        i_next = i, ++i_next;

        const std::string& pending_addr(AddrList::key(i));
        const AddrEntry& ae(AddrList::value(i));

        if (is_connected (pending_addr, UUID::nil()) == false &&
            ae.next_reconnect()                  <= now)
        {
            if (ae.retry_cnt() > ae.max_retries())
            {
                log_info << "cleaning up pending addr " << pending_addr;
                pending_addrs_.erase(i);
                continue; // no reference to pending_addr after this
            }
            else if (ae.next_reconnect() <= now)
            {
                log_debug << "connecting to pending " << pending_addr;
                gmcast_connect (pending_addr);
            }
        }
    }

    for (i = remote_addrs_.begin(); i != remote_addrs_.end(); i = i_next)
    {
        i_next = i, ++i_next;

        const std::string& remote_addr(AddrList::key(i));
        const AddrEntry& ae(AddrList::value(i));
        const UUID& remote_uuid(ae.uuid());

        gcomm_assert(remote_uuid != uuid());

        if (is_connected(remote_addr, remote_uuid) == false &&
            ae.next_reconnect()                <= now)
        {
            if (ae.retry_cnt() > ae.max_retries())
            {
                log_info << " cleaning up " << remote_uuid << " ("
                         << remote_addr << ")";
                remote_addrs_.erase(i);
                continue;//no reference to remote_addr or remote_uuid after this
            }
            else if (ae.next_reconnect() <= now)
            {
                if (ae.retry_cnt() % 30 == 0)
                {
                    log_info << self_string() << " reconnecting to "
                             << remote_uuid << " (" << remote_addr
                             << "), attempt " << ae.retry_cnt();
                }

                gmcast_connect(remote_addr);
            }
            else
            {
                //
            }
        }
    }
}

namespace
{
    class CmpUuidCounts
    {
    public:
        CmpUuidCounts(const std::set<gcomm::UUID>& uuids,
                      gcomm::SegmentId preferred_segment)
            :
            uuids_(uuids),
            preferred_segment_(preferred_segment)
        { }

        size_t count(const gcomm::gmcast::Proto* p) const
        {
            size_t cnt(0);
            for (std::set<gcomm::UUID>::const_iterator i(uuids_.begin());
                 i != uuids_.end(); ++i)
            {
                for (gcomm::gmcast::LinkMap::const_iterator
                         lm_i(p->link_map().begin());
                     lm_i != p->link_map().end(); ++lm_i)
                {
                    if (lm_i->uuid() == *i)
                    {
                        ++cnt;
                        break;
                    }
                }
            }
            return cnt;
        }
        bool operator()(const gcomm::gmcast::Proto* a,
                        const gcomm::gmcast::Proto* b) const
        {
            size_t ac(count(a));
            size_t bc(count(b));
            // if counts are equal, prefer peer from the same segment
            return (ac < bc ||
                    (ac == bc && a->remote_segment() != preferred_segment_));
        }

    private:
        const std::set<gcomm::UUID>& uuids_;
        gcomm::SegmentId preferred_segment_;
    };
}


void gcomm::GMCast::check_liveness()
{
    std::set<UUID> live_uuids;

    // iterate over proto map and mark all timed out entries as failed
    gu::datetime::Date now(gu::datetime::Date::monotonic());
    for (ProtoMap::iterator i(proto_map_->begin()); i != proto_map_->end(); )
    {
        // Store next iterator into temporary, handle_failed() may remove
        // the entry proto_map_.
        ProtoMap::iterator i_next(i);
        ++i_next;
        Proto* p(ProtoMap::value(i));
        if (p->state() > Proto::S_INIT &&
            p->state() < Proto::S_FAILED &&
            p->recv_tstamp() + peer_timeout_ < now)
        {
            gcomm::SocketStats stats(p->socket()->stats());
            log_info << self_string()
                     << " connection to peer "
                     << p->remote_uuid() << " with addr "
                     << p->remote_addr()
                     << " timed out, no messages seen in " << peer_timeout_
                     << ", socket stats: "
                     << stats;
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
        else if (p->state() == Proto::S_OK)
        {
            gcomm::SocketStats stats(p->socket()->stats());
            if (stats.send_queue_length >= 1024)
            {
                log_debug << self_string()
                          << " socket send queue to "
                          << " peer "
                          << p->remote_uuid() << " with addr "
                          << p->remote_addr()
                          << ", socket stats: "
                          << stats;
            }
            if ((p->recv_tstamp() + peer_timeout_*2/3 < now) ||
                (p->send_tstamp() + peer_timeout_*1/3 < now))
            {
                p->send_keepalive();
            }

            if (p->state() == Proto::S_FAILED)
            {
                handle_failed(p);
            }
            else
            {
                live_uuids.insert(p->remote_uuid());
            }
        }
        i = i_next;
    }

    bool should_relay(false);

    // iterate over addr list and check if there is at least one live
    // proto entry associated to each addr entry

    std::set<UUID> nonlive_uuids;
    std::string nonlive_peers;
    for (AddrList::const_iterator i(remote_addrs_.begin());
         i != remote_addrs_.end(); ++i)
    {
        const AddrEntry& ae(AddrList::value(i));
        if (ae.retry_cnt()             <= ae.max_retries() &&
            live_uuids.find(ae.uuid()) == live_uuids.end())
        {
            // log_info << self_string()
            // << " missing live proto entry for " << ae.uuid();
            nonlive_uuids.insert(ae.uuid());
            nonlive_peers += AddrList::key(i) + " ";
            should_relay = true;
        }
        else if (ae.last_connect() + peer_timeout_ > now)
        {
            log_debug << "continuing relaying for "
                      << (ae.last_connect() + peer_timeout_ - now);
            should_relay = true;
        }
    }

    if (should_relay == true)
    {
        if (relaying_ == false)
        {
            log_info << self_string()
                     << " turning message relay requesting on, nonlive peers: "
                     << nonlive_peers;
            relaying_ = true;
        }
        relay_set_.clear();
        // build set of protos having OK status
        std::set<Proto*> proto_set;
        for (ProtoMap::iterator i(proto_map_->begin()); i != proto_map_->end();
             ++i)
        {
            Proto* p(ProtoMap::value(i));
            if (p->state() == Proto::S_OK)
            {
                proto_set.insert(p);
            }
        }
        // find minimal set of proto entries required to reach maximum set
        // of nonlive peers
        while (nonlive_uuids.empty() == false &&
               proto_set.empty() == false)
        {
            std::set<Proto*>::iterator maxel(
                std::max_element(proto_set.begin(),
                                 proto_set.end(), CmpUuidCounts(nonlive_uuids, segment_)));
            Proto* p(*maxel);
            log_debug << "relay set maxel :" << *p << " count: "
                      << CmpUuidCounts(nonlive_uuids, segment_).count(p);

            relay_set_.insert(RelayEntry(p, p->socket().get()));
            const LinkMap& lm(p->link_map());
            for (LinkMap::const_iterator lm_i(lm.begin()); lm_i != lm.end();
                 ++lm_i)
            {
                nonlive_uuids.erase((*lm_i).uuid());
            }
            proto_set.erase(maxel);
        }
    }
    else if (relaying_ == true && should_relay == false)
    {
        log_info << self_string() << " turning message relay requesting off";
        relay_set_.clear();
        relaying_ = false;
    }
}


gu::datetime::Date gcomm::GMCast::handle_timers()
{
    const gu::datetime::Date now(gu::datetime::Date::monotonic());

    if (now >= next_check_)
    {
        check_liveness();
        reconnect();
        next_check_ = now + check_period_;
    }

    return next_check_;
}


void gcomm::GMCast::send(const RelayEntry& re, int segment, gcomm::Datagram& dg)
{
    int err;
    if ((err = re.socket->send(segment, dg)) != 0)
    {
        log_debug << "failed to send to " << re.socket->remote_addr()
                  << ": (" << err << ") " << strerror(err);
    }
    else if (re.proto)
    {
        re.proto->set_send_tstamp(gu::datetime::Date::monotonic());
    }
}

void gcomm::GMCast::relay(const Message& msg,
                          const Datagram& dg,
                          const void* exclude_id)
{
    Datagram relay_dg(dg);
    relay_dg.normalize();
    Message relay_msg(msg);

    // reset all relay flags from message to be relayed
    relay_msg.set_flags(relay_msg.flags() &
                        ~(Message::F_RELAY | Message::F_SEGMENT_RELAY));

    // if F_RELAY is set in received message, relay to all peers except
    // the originator
    if (msg.flags() & Message::F_RELAY)
    {
        gu_trace(push_header(relay_msg, relay_dg));
        for (SegmentMap::iterator segment_i(segment_map_.begin());
             segment_i != segment_map_.end(); ++segment_i)
        {
            Segment& segment(segment_i->second);
            for (Segment::iterator target_i(segment.begin());
                 target_i != segment.end(); ++target_i)
            {
                if ((*target_i).socket->id() != exclude_id)
                {
                    send(*target_i, msg.segment_id(), relay_dg);
                }
            }
        }
    }
    else if (msg.flags() & Message::F_SEGMENT_RELAY)
    {
        if (relay_set_.empty() == false)
        {
            // send message to all nodes in relay set to reach
            // nodes in local segment that are not directly reachable
            relay_msg.set_flags(relay_msg.flags() | Message::F_RELAY);
            gu_trace(push_header(relay_msg, relay_dg));
            for (RelaySet::iterator relay_i(relay_set_.begin());
                 relay_i != relay_set_.end(); ++relay_i)
            {
                if ((*relay_i).socket->id() != exclude_id)
                {
                    send(*relay_i, msg.segment_id(), relay_dg);
                }
            }
            gu_trace(pop_header(relay_msg, relay_dg));
            relay_msg.set_flags(relay_msg.flags() & ~Message::F_RELAY);
        }

        if (msg.segment_id() == segment_)
        {
            log_warn << "message with F_SEGMENT_RELAY from own segment, "
                     << "source " << msg.source_uuid();
        }

        // Relay to local segment
        gu_trace(push_header(relay_msg, relay_dg));
        Segment& segment(segment_map_[segment_]);
        for (Segment::iterator i(segment.begin()); i != segment.end(); ++i)
        {
            send(*i, msg.segment_id(), relay_dg);
        }
    }
    else
    {
        log_warn << "GMCast::relay() called without relay flags set";
    }
}

void gcomm::GMCast::handle_up(const void*        id,
                       const Datagram&    dg,
                       const ProtoUpMeta& um)
{
    ProtoMap::iterator i;

    if (listener_ == 0) { return; }

    if (id == listener_->id())
    {
        gmcast_accept();
    }
    else if (mcast_ && id == mcast_->id())
    {
        Message msg;

        try
        {
            if (dg.offset() < dg.header_len())
            {
                gu_trace(msg.unserialize(dg.header(), dg.header_size(),
                                         dg.header_offset() +
                                         dg.offset()));
            }
            else
            {
                gu_trace(msg.unserialize(dg.payload().data(),
                                         dg.len(),
                                         dg.offset()));
            }
        }
        catch (gu::Exception& e)
        {
            GU_TRACE(e);
            log_warn << e.what();
            return;
        }

        if (msg.type() >= Message::GMCAST_T_USER_BASE)
        {
            gu_trace(send_up(Datagram(dg, dg.offset() + msg.serial_size()),
                             ProtoUpMeta(msg.source_uuid())));
        }
        else
        {
            log_warn << "non-user message " << msg.type()
                     << " from multicast socket";
        }
    }
    else if ((i = proto_map_->find(id)) != proto_map_->end())
    {
        Proto* p(ProtoMap::value(i));

        if (dg.len() > 0)
        {
            const Proto::State prev_state(p->state());

            if (prev_state == Proto::S_FAILED)
            {
                log_warn << "unhandled failed proto";
                handle_failed(p);
                return;
            }

            Message msg;

            try
            {
                msg.unserialize(dg.payload().data(), dg.len(),
                                dg.offset());
            }
            catch (gu::Exception& e)
            {
                GU_TRACE(e);
                log_warn << e.what();
                p->set_state(Proto::S_FAILED);
                handle_failed(p);
                return;
            }

            if (msg.type() >= Message::GMCAST_T_USER_BASE)
            {
                if (evict_list().empty() == false &&
                    evict_list().find(msg.source_uuid()) != evict_list().end())
                {
                    return;
                }
                if (msg.flags() &
                    (Message::F_RELAY | Message::F_SEGMENT_RELAY))
                {
                    relay(msg,
                          Datagram(dg, dg.offset() + msg.serial_size()),
                          id);
                }
                p->set_recv_tstamp(gu::datetime::Date::monotonic());
                send_up(Datagram(dg, dg.offset() + msg.serial_size()),
                        ProtoUpMeta(msg.source_uuid()));
                return;
            }
            else
            {
                try
                {
                    p->set_recv_tstamp(gu::datetime::Date::monotonic());
                    gu_trace(p->handle_message(msg));
                }
                catch (const gu::Exception& e)
                {
                    handle_failed(p);
                    if (e.get_errno() == ENOTRECOVERABLE)
                    {
                        throw;
                    }
                    log_warn
                        << "handling gmcast protocol message failed: "
                        << e.what();
                    return;
                }

                if (p->state() == Proto::S_FAILED)
                {
                    handle_failed(p);
                    return;
                }
                else if (p->check_changed_and_reset() == true)
                {
                    update_addresses();
                    check_liveness();
                    reconnect();
                }
            }

            if (prev_state != Proto::S_OK && p->state() == Proto::S_OK)
            {
                handle_established(p);
            }
        }
        else if (p->socket()->state() == Socket::S_CONNECTED &&
                 (p->state() == Proto::S_HANDSHAKE_WAIT ||
                  p->state() == Proto::S_INIT))
        {
            handle_connected(p);
        }
        else if (p->socket()->state() == Socket::S_CONNECTED)
        {
            log_warn << "connection " << p->socket()->id()
                     << " closed by peer";
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
        else
        {
            log_debug << "socket in state " << p->socket()->state();
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
    }
    else
    {
        // log_info << "proto entry " << id << " not found";
    }
}

static gcomm::gmcast::Proto* find_by_remote_uuid(
    const gcomm::gmcast::ProtoMap& proto_map,
    const gcomm::UUID& uuid)
{
    for (gcomm::gmcast::ProtoMap::const_iterator i(proto_map.begin());
         i != proto_map.end(); ++i)
    {
        if (i->second->remote_uuid() == uuid)
        {
            return i->second;
        }
    }
    return 0;
}

int gcomm::GMCast::handle_down(Datagram& dg, const ProtoDownMeta& dm)
{
    Message msg(version_, Message::GMCAST_T_USER_BASE, uuid(), 1, segment_);

    // If target is set and proto entry for target is found,
    // send a direct message. Otherwise fall back for broadcast
    // to ensure message delivery via relay
    if (dm.target() != UUID::nil())
    {
        Proto* target_proto(find_by_remote_uuid(*proto_map_, dm.target()));
        if (target_proto && target_proto->state() == Proto::S_OK)
        {
            gu_trace(push_header(msg, dg));
            int err;
            if ((err = target_proto->socket()->send(msg.segment_id(), dg)) != 0)
            {
                log_debug << "failed to send to "
                          << target_proto->socket()->remote_addr()
                          << ": (" << err << ") " << strerror(err);
            }
            else
            {
                target_proto->set_send_tstamp(gu::datetime::Date::monotonic());
            }
            gu_trace(pop_header(msg, dg));
            if (err == 0)
            {
                return 0;
            }
            // In case of error fall back to broadcast
        }
        else
        {
            log_debug << "Target " << dm.target() << " proto not found";
        }
    }

    // handle relay set first, skip these peers below
    if (relay_set_.empty() == false)
    {
        msg.set_flags(msg.flags() | Message::F_RELAY);
        gu_trace(push_header(msg, dg));
        for (RelaySet::iterator ri(relay_set_.begin());
             ri != relay_set_.end(); ++ri)
        {
            send(*ri, msg.segment_id(), dg);
        }
        gu_trace(pop_header(msg, dg));
        msg.set_flags(msg.flags() & ~Message::F_RELAY);
    }

    for (SegmentMap::iterator si(segment_map_.begin());
         si != segment_map_.end(); ++si)
    {
        uint8_t segment_id(si->first);
        Segment& segment(si->second);

        if (segment_id != segment_)
        {
            size_t target_idx((self_index_ + segment_id) % segment.size());
            msg.set_flags(msg.flags() | Message::F_SEGMENT_RELAY);
            // skip peers that are in relay set
            if (relay_set_.empty() == true ||
                relay_set_.find(segment[target_idx]) == relay_set_.end())
            {
                gu_trace(push_header(msg, dg));
                send(segment[target_idx], msg.segment_id(), dg);
                gu_trace(pop_header(msg, dg));
            }
        }
        else
        {
            msg.set_flags(msg.flags() & ~Message::F_SEGMENT_RELAY);
            gu_trace(push_header(msg, dg));
            for (Segment::iterator i(segment.begin());
                 i != segment.end(); ++i)
            {
                // skip peers that are in relay set
                if (relay_set_.empty() == true ||
                    relay_set_.find(*i) == relay_set_.end())
                {
                    send(*i, msg.segment_id(), dg);
                }
            }
            gu_trace(pop_header(msg, dg));
        }
    }

    return 0;
}

void gcomm::GMCast::handle_stable_view(const View& view)
{
    log_debug << "GMCast::handle_stable_view: " << view;
    if (view.type() == V_PRIM)
    {
        // discard addr list entries not in view
        std::set<UUID> gmcast_lst;
        for (AddrList::const_iterator i(remote_addrs_.begin());
             i != remote_addrs_.end(); ++i)
        {
            gmcast_lst.insert(i->second.uuid());
        }
        std::set<UUID> view_lst;
        for (NodeList::const_iterator i(view.members().begin());
             i != view.members().end(); ++i)
        {
            view_lst.insert(i->first);
        }
        std::list<UUID> diff;
        std::set_difference(gmcast_lst.begin(),
                            gmcast_lst.end(),
                            view_lst.begin(),
                            view_lst.end(),
                            std::back_inserter(diff));

        // Forget partitioned entries, allow them to reconnect
        // in time_wait_/2. Left nodes are given time_wait_ ban for
        // reconnecting when handling V_REG below.
        for (std::list<UUID>::const_iterator i(diff.begin());
             i != diff.end(); ++i)
        {
            gmcast_forget(*i, time_wait_/2);
        }

        // mark nodes in view as stable
        for (std::set<UUID>::const_iterator i(view_lst.begin());
             i != view_lst.end(); ++i)
        {
            AddrList::iterator ai;
            if ((ai = find_if(remote_addrs_.begin(), remote_addrs_.end(),
                              AddrListUUIDCmp(*i))) != remote_addrs_.end())
            {
                ai->second.set_retry_cnt(-1);
                ai->second.set_max_retries(max_retry_cnt_);
            }
        }

        // iterate over pending address list and discard entries without UUID
        for (AddrList::iterator i(pending_addrs_.begin());
             i != pending_addrs_.end(); )
        {
            AddrList::iterator i_next(i);
            ++i_next;
            const AddrEntry& ae(AddrList::value(i));
            if (ae.uuid() == UUID())
            {
                const std::string addr(AddrList::key(i));
                log_info << "discarding pending addr without UUID: "
                         << addr;
                for (ProtoMap::iterator pi(proto_map_->begin());
                     pi != proto_map_->end();)
                {
                    ProtoMap::iterator pi_next(pi);
                    ++pi_next;
                    Proto* p(ProtoMap::value(pi));
                    if (p->remote_addr() == addr)
                    {
                        log_info << "discarding pending addr proto entry " << p;
                        erase_proto(pi);
                    }
                    pi = pi_next;
                }
                pending_addrs_.erase(i);
            }
            i = i_next;
        }
        prim_view_reached_ = true;
    }
    else if (view.type() == V_REG)
    {
        for (NodeList::const_iterator i(view.members().begin());
             i != view.members().end(); ++i)
        {
            AddrList::iterator ai;
            if ((ai = find_if(remote_addrs_.begin(), remote_addrs_.end(),
                              AddrListUUIDCmp(NodeList::key(i))))
                != remote_addrs_.end())
            {
                log_info << "declaring " << NodeList::key(i)
                         << " at " << handle_get_address(NodeList::key(i))
                         << " stable";
                ai->second.set_retry_cnt(-1);
                ai->second.set_max_retries(max_retry_cnt_);
            }
        }

        // Forget left nodes
        for (NodeList::const_iterator i(view.left().begin());
             i != view.left().end(); ++i)
        {
            gmcast_forget(NodeList::key(i), time_wait_);
        }
    }
    check_liveness();

    for (ProtoMap::const_iterator i(proto_map_->begin()); i != proto_map_->end();
         ++i)
    {
        log_debug << "proto: " << *ProtoMap::value(i);
    }
}


void gcomm::GMCast::handle_evict(const UUID& uuid)
{
    if (is_evicted(uuid) == true)
    {
        return;
    }
    gmcast_forget(uuid, time_wait_);
}


std::string gcomm::GMCast::handle_get_address(const UUID& uuid) const
{
    AddrList::const_iterator ali(
        find_if(remote_addrs_.begin(),
                remote_addrs_.end(),
                AddrListUUIDCmp(uuid)));
    return (ali == remote_addrs_.end() ? "" : AddrList::key(ali));
}

void gcomm::GMCast::add_or_del_addr(const std::string& val)
{
    if (val.compare(0, 4, "add:") == 0)
    {
        gu::URI uri(val.substr(4));
        std::string addr(gu::net::resolve(uri_string(get_scheme(use_ssl_),
                                                     uri.get_host(),
                                                     uri.get_port())).to_string());
        log_info << "inserting address '" << addr << "'";
        insert_address(addr, UUID(), remote_addrs_);
        AddrList::iterator ai(remote_addrs_.find(addr));
        AddrList::value(ai).set_max_retries(
            max_initial_reconnect_attempts_);
        AddrList::value(ai).set_retry_cnt(-1);
    }
    else if (val.compare(0, 4, "del:") == 0)
    {
        std::string addr(val.substr(4));
        AddrList::iterator ai(remote_addrs_.find(addr));
        if (ai != remote_addrs_.end())
        {
            ProtoMap::iterator pi, pi_next;
            for (pi = proto_map_->begin(); pi != proto_map_->end(); pi = pi_next)
            {
                pi_next = pi, ++pi_next;
                Proto* rp = ProtoMap::value(pi);
                if (rp->remote_addr() == AddrList::key(ai))
                {
                    log_info << "deleting entry " << AddrList::key(ai);
                    erase_proto(pi);
                }
            }
            AddrEntry& ae(AddrList::value(ai));
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            ae.set_next_reconnect(gu::datetime::Date::monotonic() + time_wait_);
            update_addresses();
        }
        else
        {
            log_info << "address '" << addr
                     << "' not found from remote addrs list";
        }
    }
    else
    {
        gu_throw_error(EINVAL) << "invalid addr spec '" << val << "'";
    }
}


bool gcomm::GMCast::set_param(const std::string& key, const std::string& val,
                              Protolay::sync_param_cb_t& sync_param_cb)
{
    try
    {
        if (key == Conf::GMCastMaxInitialReconnectAttempts)
        {
            max_initial_reconnect_attempts_ = gu::from_string<int>(val);
            return true;
        }
        else if (key == Conf::GMCastPeerAddr)
        {
            try
            {
                add_or_del_addr(val);
            }
            catch (gu::NotFound& nf)
            {
                gu_throw_error(EINVAL) << "invalid addr spec '" << val << "'";
            }
            catch (gu::NotSet& ns)
            {
                gu_throw_error(EINVAL) << "invalid addr spec '" << val << "'";
            }
            return true;
        }
        else if (key == Conf::GMCastIsolate)
        {
            int tmpval = gu::from_string<int>(val);
            if (tmpval < 0 || tmpval > 2)
            {
                gu_throw_error(EINVAL)
                    << "invalid value for gmacst.isolate: '"
                    << tmpval << "'";
            }
            isolate_ = tmpval;
            log_info << "turning isolation "
                     << (isolate_ == 1 ? "on" :
                         (isolate_ == 2 ? "force quit" : "off"));
            if (isolate_)
            {
                // delete all entries in proto map
                ProtoMap::iterator pi, pi_next;
                for (pi = proto_map_->begin(); pi != proto_map_->end();
                     pi = pi_next)
                {
                    pi_next = pi, ++pi_next;
                    erase_proto(pi);
                }
                segment_map_.clear();
            }
            return true;
        }
        else if (key == Conf::SocketRecvBufSize)
        {
            gu_trace(Conf::check_recv_buf_size(val));
            conf_.set(key, val);

            for (ProtoMap::iterator pi(proto_map_->begin());
                 pi != proto_map_->end(); ++pi)
            {
                gu_trace(pi->second->socket()->set_option(key, val));
                // erase_proto(pi++);
            }
            // segment_map_.clear();
            // reconnect();
            return true;
        }
        else if (key == Conf::GMCastGroup       ||
                 key == Conf::GMCastListenAddr  ||
                 key == Conf::GMCastMCastAddr   ||
                 key == Conf::GMCastMCastPort   ||
                 key == Conf::GMCastMCastTTL    ||
                 key == Conf::GMCastTimeWait    ||
                 key == Conf::GMCastPeerTimeout ||
                 key == Conf::GMCastSegment)
        {
            gu_throw_error(EPERM) << "can't change value during runtime";
        }
    }
    catch (gu::Exception& e)
    {
        GU_TRACE(e); throw;
    }
    catch (std::exception& e)
    {
        gu_throw_error(EINVAL) << e.what();
    }
    catch (...)
    {
        gu_throw_error(EINVAL) << "exception";
    }

    return false;
}
