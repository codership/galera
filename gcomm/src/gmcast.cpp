/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
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
    return (uri.get_scheme() == gcomm::TCP_SCHEME ||
            uri.get_scheme() == gcomm::SSL_SCHEME);
}

static std::string get_scheme(bool use_ssl)
{
    if (use_ssl == true)
    {
        return gcomm::SSL_SCHEME;
    }
    return gcomm::TCP_SCHEME;
}


gcomm::GMCast::GMCast(Protonet& net, const gu::URI& uri)
    :
    Transport     (net, uri),
    version_(check_range(Conf::GMCastVersion,
                         param<int>(conf_, uri, Conf::GMCastVersion, "0"),
                         0, max_version_ + 1)),
    segment_ (check_range(Conf::GMCastSegment,
                          param<int>(conf_, uri, Conf::GMCastSegment, "0"),
                          0, 255)),
    my_uuid_      (0, 0),
    use_ssl_      (param<bool>(conf_, uri, Conf::SocketUseSsl, "false")),
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
    listener_     (0),
    mcast_        (),
    pending_addrs_(),
    remote_addrs_ (),
    addr_blacklist_(),
    relaying_     (false),
    isolate_      (false),
    proto_map_    (new ProtoMap()),
    relay_set_    (),
    segment_map_  (),
    self_index_   (std::numeric_limits<size_t>::max()),
    time_wait_    (param<gu::datetime::Period>(conf_, uri, Conf::GMCastTimeWait, "PT5S")),
    check_period_ ("PT0.5S"),
    peer_timeout_ (param<gu::datetime::Period>(conf_, uri, Conf::GMCastPeerTimeout, "PT3S")),
    max_initial_reconnect_attempts_(
        param<int>(conf_, uri,
                   Conf::GMCastMaxInitialReconnectAttempts,
                   gu::to_string(max_retry_cnt_))),
    next_check_   (gu::datetime::Date::now())
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
        listen_addr_.replace(0, 3, gcomm::SSL_SCHEME);
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
            port = uri_.get_option(Conf::GMCastMCastPort);
        }
        catch (gu::NotFound&) {}

        mcast_addr_ = gu::net::resolve(
            uri_string(gcomm::UDP_SCHEME, mcast_addr_, port)).to_string();
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
        catch (gu::NotSet& )
        {
            try
            {
                port = conf_.get(BASE_PORT_KEY);
            }
            catch (gu::NotFound&)
            {
                port = Defaults::GMCastTcpPort;
            }
        }
        std::string initial_addr = gu::net::resolve(
            uri_string(get_scheme(use_ssl_), host, port)
            ).to_string();

        // resolving sets scheme to tcp, have to rewrite for ssl
        if (use_ssl_ == true)
        {
            initial_addr.replace(0, 3, gcomm::SSL_SCHEME);
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
    if (mcast_ != 0)
    {
        mcast_->close();
        // delete mcast;
        // mcast = 0;
    }

    gcomm_assert(listener_ != 0);
    listener_->close();
    delete listener_;
    listener_ = 0;

    segment_map_.clear();
    for (ProtoMap::iterator
             i = proto_map_->begin(); i != proto_map_->end(); ++i)
    {
        delete ProtoMap::value(i);
    }

    proto_map_->clear();
    pending_addrs_.clear();
    remote_addrs_.clear();
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

    if (isolate_ == true)
    {
        log_debug << "dropping accepted socket due to isolation";
        tp->close();
        return;
    }

    Proto* peer = new Proto (
        version_, tp,
        listener_->listen_addr() /* listen_addr */,
        "", mcast_addr_,
        uuid(),
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
        version_,
        tp,
        listener_->listen_addr()/* listen_addr*/ ,
        remote_addr,
        mcast_addr_,
        uuid(),
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


void gcomm::GMCast::gmcast_forget(const UUID& uuid)
{
    /* Close all proto entries corresponding to uuid */

    ProtoMap::iterator pi, pi_next;
    for (pi = proto_map_->begin(); pi != proto_map_->end(); pi = pi_next)
    {
        pi_next = pi, ++pi_next;
        Proto* rp = ProtoMap::value(pi);
        if (rp->remote_uuid() == uuid)
        {
            delete rp;
            proto_map_->erase(pi);
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
                Proto* rp = ProtoMap::value(pi);
                if (rp->remote_addr() == AddrList::key(ai))
                {
                    log_info << "deleting entry " << AddrList::key(ai);
                    delete rp;
                    proto_map_->erase(pi);
                }
            }
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            ae.set_next_reconnect(gu::datetime::Date::now() + time_wait_);
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
        log_debug << "sending hanshake";
        // accepted socket was waiting for underlying transport
        // handshake to finish
        rp->send_handshake();
    }
}

void gcomm::GMCast::handle_established(Proto* est)
{
    log_debug << self_string() << " connection established to "
              << est->remote_uuid() << " "
              << est->remote_addr();

    if (est->remote_uuid() == uuid())
    {
        std::set<std::string>::iterator
            ia_i(initial_addrs_.find(est->remote_addr()));
        if (ia_i != initial_addrs_.end())
        {
            initial_addrs_.erase(ia_i);
        }
        AddrList::iterator i(pending_addrs_.find(est->remote_addr()));
        if (i != pending_addrs_.end())
        {
            if (addr_blacklist_.find(est->remote_addr()) == addr_blacklist_.end())
            {
                log_warn << self_string()
                         << " address '" << est->remote_addr()
                         << "' points to own listening address, blacklisting";
            }
            pending_addrs_.erase(i);
            addr_blacklist_.insert(make_pair(est->remote_addr(),
                                             AddrEntry(gu::datetime::Date::now(),
                                                       gu::datetime::Date::now(),
                                                       est->remote_uuid())));
        }
        proto_map_->erase(
            proto_map_->find_checked(est->socket()->id()));
        delete est;
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
                 << " changed identity " << AddrList::value(i).uuid()
                 << " -> " << est->remote_uuid();
        remote_addrs_.erase(i);
        i = remote_addrs_.insert_unique(
            make_pair(est->remote_addr(),
                      AddrEntry(gu::datetime::Date::now(),
                                gu::datetime::Date::max(),
                                est->remote_uuid())));
    }

    if (AddrList::value(i).retry_cnt() >
        AddrList::value(i).max_retries())
    {
        log_warn << "discarding established (time wait) "
                 << est->remote_uuid()
                 << " (" << est->remote_addr() << ") ";
        proto_map_->erase(proto_map_->find(est->socket()->id()));
        delete est;
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
                log_info << self_string()
                          << " cleaning up duplicate "
                          << p->socket()
                          << " after established "
                          << est->socket();
                proto_map_->erase(j);
                delete p;
            }
            else if (p->handshake_uuid() > est->handshake_uuid())
            {
                log_info << self_string()
                         << " cleaning up established "
                         << est->socket()
                         << " which is duplicate of "
                         << p->socket();
                proto_map_->erase(
                    proto_map_->find_checked(est->socket()->id()));
                delete est;
                break;
            }
            else
            {
                assert(p == est);
            }
        }
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

            gu::datetime::Date rtime = gu::datetime::Date::now() + gu::datetime::Period("PT1S");
            log_debug << self_string()
                      << " setting next reconnect time to "
                      << rtime << " for " << remote_addr;
            ae.set_next_reconnect(rtime);
        }
    }

    std::set<Socket*>::iterator si(relay_set_.find(failed->socket().get()));
    if (si != relay_set_.end())
    {
        relay_set_.erase(si);
    }
    proto_map_->erase(failed->socket()->id());
    delete failed;
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
                               AddrEntry(gu::datetime::Date::now(),
                                         gu::datetime::Date::now(), uuid))).second == false)
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
                proto_map_->erase(i);
                delete rp;
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
                    log_info << self_string()
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
                    gu::datetime::Date rtime(gu::datetime::Date::now());

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

    if (mcast_ != 0)
    {
        log_debug << mcast_addr_;
        local_segment.push_back(mcast_.get());
    }

    self_index_ = 0;
    for (ProtoMap::const_iterator i(proto_map_->begin()); i != proto_map_->end();
         ++i)
    {
        const Proto& p(*ProtoMap::value(i));

        log_debug << "Proto: " << p;

        if (p.remote_segment() == segment_)
        {
            if (p.state() == Proto::S_OK &&
                (p.mcast_addr() == "" ||
                 p.mcast_addr() != mcast_addr_))
            {
                local_segment.push_back(p.socket().get());
                if (p.remote_uuid() < uuid())
                {
                    ++self_index_;
                }
            }
        }
        else
        {
            if (p.state() == Proto::S_OK)
            {
                Segment& remote_segment(segment_map_[p.remote_segment()]);
                remote_segment.push_back(p.socket().get());
            }
        }
    }
    log_debug << self_string() << " self index: " << self_index_;
    log_debug << self_string() << " --- mcast tree end ---";
}


void gcomm::GMCast::reconnect()
{
    if (isolate_ == true)
    {
        log_debug << "skipping reconnect due to isolation";
        return;
    }

    /* Loop over known remote addresses and connect if proto entry
     * does not exist */
    gu::datetime::Date now = gu::datetime::Date::now();
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
    gu::datetime::Date now(gu::datetime::Date::now());
    for (ProtoMap::iterator i(proto_map_->begin()); i != proto_map_->end(); )
    {
        ProtoMap::iterator i_next(i);
        ++i_next;
        Proto* p(ProtoMap::value(i));
        if (p->state() > Proto::S_INIT &&
            p->state() < Proto::S_FAILED &&
            p->tstamp() + peer_timeout_ < now)
        {
            log_debug << self_string()
                      << " connection to peer "
                      << p->remote_uuid() << " with addr "
                      << p->remote_addr()
                      << " timed out";
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
        else if (p->state() == Proto::S_OK)
        {
            if (p->tstamp() + peer_timeout_*2/3 < now)
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

            relay_set_.insert(p->socket().get());
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
    const gu::datetime::Date now(gu::datetime::Date::now());

    if (now >= next_check_)
    {
        check_liveness();
        reconnect();
        next_check_ = now + check_period_;
    }

    return next_check_;
}


void send(gcomm::Socket* s, gcomm::Datagram& dg)
{
    int err;
    if ((err = s->send(dg)) != 0)
    {
        log_debug << "failed to send to " << s->remote_addr()
                  << ": (" << err << ") " << strerror(err);
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
        for (SegmentMap::iterator i(segment_map_.begin());
             i != segment_map_.end(); ++i)
        {
            Segment& segment(i->second);
            for (Segment::iterator j(segment.begin()); j != segment.end(); ++j)
            {
                if ((*j)->id() != exclude_id)
                {
                    send(*j, relay_dg);
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
            for (std::set<Socket*>::iterator ri(relay_set_.begin());
                 ri != relay_set_.end(); ++ri)
            {
                send(*ri, relay_dg);
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
            send(*i, relay_dg);
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
    else if (mcast_.get() != 0 && id == mcast_->id())
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
                gu_trace(msg.unserialize(&dg.payload()[0],
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

        if (msg.type() >= Message::T_USER_BASE)
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
                msg.unserialize(&dg.payload()[0], dg.len(),
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

            if (msg.type() >= Message::T_USER_BASE)
            {
                if (msg.flags() &
                    (Message::F_RELAY | Message::F_SEGMENT_RELAY))
                {
                    relay(msg,
                          Datagram(dg, dg.offset() + msg.serial_size()),
                          id);
                }
                p->set_tstamp(gu::datetime::Date::now());
                send_up(Datagram(dg, dg.offset() + msg.serial_size()),
                        ProtoUpMeta(msg.source_uuid()));
                return;
            }
            else
            {
                try
                {
                    p->set_tstamp(gu::datetime::Date::now());
                    gu_trace(p->handle_message(msg));
                }
                catch (gu::Exception& e)
                {
                    log_warn << "handling gmcast protocol message failed: "
                             << e.what();
                    handle_failed(p);
                    return;
                }

                if (p->state() == Proto::S_FAILED)
                {
                    handle_failed(p);
                    return;
                }
                else if (p->changed() == true)
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


int gcomm::GMCast::handle_down(Datagram& dg, const ProtoDownMeta& dm)
{
    Message msg(version_, Message::T_USER_BASE, uuid(), 1, segment_);

    // handle relay set first, skip these peers below
    if (relay_set_.empty() == false)
    {
        msg.set_flags(msg.flags() | Message::F_RELAY);
        gu_trace(push_header(msg, dg));
        for (std::set<Socket*>::iterator ri(relay_set_.begin());
             ri != relay_set_.end(); ++ri)
        {
            send(*ri, dg);
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
                send(segment[target_idx], dg);
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
                    send(*i, dg);
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

        for (std::list<UUID>::const_iterator i(diff.begin());
             i != diff.end(); ++i)
        {
            gmcast_forget(*i);
        }

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
                        delete p;
                        proto_map_->erase(pi);
                    }
                    pi = pi_next;
                }
                pending_addrs_.erase(i);
            }
            i = i_next;
        }
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
                log_info << "declaring " << NodeList::key(i) << " stable";
                ai->second.set_retry_cnt(-1);
                ai->second.set_max_retries(max_retry_cnt_);
            }
        }
    }
    check_liveness();

    for (ProtoMap::const_iterator i(proto_map_->begin()); i != proto_map_->end();
         ++i)
    {
        log_debug << "proto: " << *ProtoMap::value(i);
    }
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
                    delete rp;
                    proto_map_->erase(pi);
                }
            }
            AddrEntry& ae(AddrList::value(ai));
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            ae.set_next_reconnect(gu::datetime::Date::now() + time_wait_);
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


bool gcomm::GMCast::set_param(const std::string& key, const std::string& val)
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
        isolate_ = gu::from_string<bool>(val);
        log_info << "turning isolation "
                 << (isolate_ == true ? "on" : "off");
        if (isolate_ == true)
        {
            // delete all entries in proto map
            ProtoMap::iterator pi, pi_next;
            for (pi = proto_map_->begin(); pi != proto_map_->end(); pi = pi_next)
            {
                pi_next = pi, ++pi_next;
                Proto* rp = ProtoMap::value(pi);
                delete rp;
                proto_map_->erase(pi);
            }
            segment_map_.clear();
        }
        return true;
    }
    else if (key == Conf::GMCastGroup ||
             key == Conf::GMCastListenAddr ||
             key == Conf::GMCastMCastAddr ||
             key == Conf::GMCastMCastPort ||
             key == Conf::GMCastMCastTTL ||
             key == Conf::GMCastTimeWait ||
             key == Conf::GMCastPeerTimeout ||
             key == Conf::GMCastSegment)
    {
        gu_throw_error(EPERM) << "can't change value for '"
                              << key << "' during runtime";
    }
    return false;
}
