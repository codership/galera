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

using namespace std;
using namespace std::rel_ops;
using namespace gcomm;
using namespace gcomm::gmcast;
using namespace gu;
using namespace gu::net;
using namespace gu::datetime;

const long gcomm::GMCast::max_retry_cnt(std::numeric_limits<int>::max());

static void set_tcp_defaults (URI* uri)
{
    // what happens if there is already this parameter?
    uri->set_option(Conf::TcpNonBlocking, gu::to_string(1));
}


static bool check_tcp_uri(const URI& uri)
{
    return (uri.get_scheme() == TCP_SCHEME ||
            uri.get_scheme() == SSL_SCHEME);
}

static std::string get_scheme(bool use_ssl)
{
    if (use_ssl == true)
    {
        return SSL_SCHEME;
    }
    return TCP_SCHEME;
}


GMCast::GMCast(Protonet& net, const gu::URI& uri)
    :
    Transport     (net, uri),
    version(check_range(Conf::GMCastVersion,
                        param<int>(conf_, uri, Conf::GMCastVersion, "0"),
                        0, max_version_ + 1)),
    my_uuid       (0, 0),
    use_ssl       (param<bool>(conf_, uri, Conf::SocketUseSsl, "false")),
    // @todo: technically group name should be in path component
    group_name    (param<std::string>(conf_, uri, Conf::GMCastGroup, "")),
    listen_addr   (
        param<std::string>(
            conf_, uri, Conf::GMCastListenAddr,
            get_scheme(use_ssl) + "://0.0.0.0")), // how to make it IPv6 safe?
    initial_addr  (""),
    mcast_addr    (param<std::string>(conf_, uri, Conf::GMCastMCastAddr, "")),
    bind_ip       (""),
    mcast_ttl     (check_range(
                       Conf::GMCastMCastTTL,
                       param<int>(conf_, uri, Conf::GMCastMCastTTL, "1"),
                       1, 256)),
    listener      (0),
    mcast         (),
    pending_addrs (),
    remote_addrs  (),
    addr_blacklist(),
    relaying      (false),
    proto_map     (new ProtoMap()),
    mcast_tree    (),
    time_wait     (param<Period>(conf_, uri, Conf::GMCastTimeWait, "PT5S")),
    check_period  ("PT0.5S"),
    peer_timeout  (param<Period>(conf_, uri, Conf::GMCastPeerTimeout, "PT3S")),
    max_initial_reconnect_attempts(
        param<int>(conf_, uri,
                   Conf::GMCastMaxInitialReconnectAttempts,
                   Defaults::GMCastMaxInitialReconnectAttempts)),
    next_check    (Date::now())
{
    log_info << "GMCast version " << version;

    if (group_name == "")
    {
        gu_throw_error (EINVAL) << "Group not defined in URL: "
                                << uri_.to_string();
    }

    set_initial_addr(uri_);

    try
    {
        listen_addr = uri_.get_option (Conf::GMCastListenAddr);
    }
    catch (gu::NotFound&) {}

    try
    {
        gu::URI uri(listen_addr); /* check validity of the address */
    }
    catch (Exception&)
    {
        /* most probably no scheme, try to append one and see if it succeeds */
        listen_addr = uri_string(get_scheme(use_ssl), listen_addr);
        gu_trace(gu::URI uri(listen_addr));
    }

    URI listen_uri(listen_addr);

    if (check_tcp_uri(listen_uri) == false)
    {
        gu_throw_error (EINVAL) << "listen addr '" << listen_addr
                                << "' does not specify supported protocol";
    }

    if (resolve(listen_uri).get_addr().is_anyaddr() == false)
    {
        // bind outgoing connections to the same address as listening.
        gu_trace(bind_ip = listen_uri.get_host());
    }

    string port(Defaults::GMCastTcpPort);

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
        catch (gu::NotFound&)
        {
            // if no base port configured, try port from the connection address
            try { port = uri_.get_port(); } catch (gu::NotSet&) {}
        }

        listen_addr += ":" + port;
    }

    // if (!conf_.has(BASE_PORT_KEY)) {
        conf_.set(BASE_PORT_KEY, port);
    // }

    listen_addr = resolve(listen_addr).to_string();
    // resolving sets scheme to tcp, have to rewrite for ssl
    if (use_ssl == true)
    {
        listen_addr.replace(0, 3, SSL_SCHEME);
    }

    if (listen_addr == initial_addr)
    {
        gu_throw_error(EINVAL) << "connect address points to listen address '"
                               << listen_addr
                               << "', check that cluster address '"
                               << uri.get_host() << ":" << port
                               << "' is correct";
    }

    if (mcast_addr != "")
    {
        try
        {
            port = uri_.get_option(Conf::GMCastMCastPort);
        }
        catch (NotFound&) {}

        mcast_addr = resolve(uri_string(UDP_SCHEME, mcast_addr, port)).to_string();
    }

    log_info << self_string() << " listening at " << listen_addr;
    log_info << self_string() << " multicast: " << mcast_addr
             << ", ttl: " << mcast_ttl;

    conf_.set(Conf::GMCastListenAddr, listen_addr);
    conf_.set(Conf::GMCastMCastAddr, mcast_addr);
    conf_.set(Conf::GMCastVersion, gu::to_string(version));
    conf_.set(Conf::GMCastTimeWait, gu::to_string(time_wait));
    conf_.set(Conf::GMCastMCastTTL, gu::to_string(mcast_ttl));
    conf_.set(Conf::GMCastPeerTimeout, gu::to_string(peer_timeout));

}

GMCast::~GMCast()
{
    if (listener != 0) close();

    delete proto_map;
}

void gcomm::GMCast::set_initial_addr(const gu::URI& uri)
{
    try
    {
        if (!host_is_any(uri.get_host()))
        {
            string port;

            try
            {
                port = uri.get_port();
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

            initial_addr = resolve(
                uri_string(get_scheme(use_ssl), uri.get_host(), port)
                ).to_string();

            // resolving sets scheme to tcp, have to rewrite for ssl
            if (use_ssl == true)
            {
                initial_addr.replace(0, 3, SSL_SCHEME);
            }

            if (check_tcp_uri(initial_addr) == false)
            {
                gu_throw_error (EINVAL) << "initial addr '" << initial_addr
                                        << "' is not valid";
            }

            log_debug << self_string() << " initial addr: " << initial_addr;
        }
    }
    catch (gu::NotSet&)
    {
        //@note: this is different from empty host and indicates URL without
        //       ://
        gu_throw_error (EINVAL) << "Host not defined in URL: "
                                << uri.to_string();
    }
}


void GMCast::connect()
{
    pstack_.push_proto(this);
    log_debug << "gmcast " << get_uuid() << " connect";

    URI listen_uri(listen_addr);

    set_tcp_defaults (&listen_uri);

    listener = get_pnet().acceptor(listen_uri);
    gu_trace (listener->listen(listen_uri));

    if (!mcast_addr.empty())
    {
        URI mcast_uri(
            mcast_addr + '?'
            + gcomm::Socket::OptIfAddr + '=' + URI(listen_addr).get_host()+'&'
            + gcomm::Socket::OptNonBlocking + "=1&"
            + gcomm::Socket::OptMcastTTL    + '=' + to_string(mcast_ttl)
            );

        mcast = get_pnet().socket(mcast_uri);
        gu_trace(mcast->connect(mcast_uri));
    }

    if (!initial_addr.empty())
    {
        insert_address(initial_addr, UUID(), pending_addrs);
        AddrList::iterator ai(pending_addrs.find(initial_addr));
        AddrList::get_value(ai).set_max_retries(max_retry_cnt);
        gu_trace (gmcast_connect(initial_addr));
    }
}


void gcomm::GMCast::connect(const gu::URI& uri)
{
    set_initial_addr(uri);
    connect();
}



void GMCast::close(bool force)
{
    log_debug << "gmcast " << get_uuid() << " close";
    pstack_.pop_proto(this);
    if (mcast != 0)
    {
        mcast->close();
        // delete mcast;
        // mcast = 0;
    }

    gcomm_assert(listener != 0);
    listener->close();
    delete listener;
    listener = 0;

    mcast_tree.clear();
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        delete ProtoMap::get_value(i);
    }

    proto_map->clear();
    pending_addrs.clear();
    remote_addrs.clear();
}


void GMCast::gmcast_accept()
{
    SocketPtr tp;

    try
    {
        tp = listener->accept();
    }
    catch (Exception& e)
    {
        log_warn << e.what();
        return;
    }

    Proto* peer = new Proto (version, tp,
                             listener->listen_addr() /* listen_addr */,
                             "", mcast_addr,
                             get_uuid(), group_name);
    pair<ProtoMap::iterator, bool> ret =
        proto_map->insert(make_pair(tp->get_id(), peer));

    if (ret.second == false)
    {
        delete peer;
        gu_throw_fatal << "Failed to add peer to map";
    }
    if (tp->get_state() == Socket::S_CONNECTED)
    {
        peer->send_handshake();
    }
    else
    {
        log_debug << "accepted socket is connecting";
    }
    log_debug << "handshake sent";
}


void GMCast::gmcast_connect(const string& remote_addr)
{
    if (remote_addr == listen_addr) return;

    URI connect_uri(remote_addr);

    set_tcp_defaults (&connect_uri);

    if (!bind_ip.empty())
    {
        connect_uri.set_option(gcomm::Socket::OptIfAddr, bind_ip);
    }

    SocketPtr tp = get_pnet().socket(connect_uri);

    try
    {
        tp->connect(connect_uri);
    }
    catch (Exception& e)
    {
        log_debug << "Connect failed: " << e.what();
        // delete tp;
        return;
    }

    Proto* peer = new Proto (version,
                             tp,
                             listener->listen_addr()/* listen_addr*/ ,
                             remote_addr,
                             mcast_addr,
                             get_uuid(),
                             group_name);

    pair<ProtoMap::iterator, bool> ret =
        proto_map->insert(make_pair(tp->get_id(), peer));

    if (ret.second == false)
    {
        delete peer;
        gu_throw_fatal << "Failed to add peer to map";
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
        Proto* rp = ProtoMap::get_value(pi);
        if (rp->get_remote_uuid() == uuid)
        {
            delete rp;
            proto_map->erase(pi);
        }
    }

    /* Set all corresponding entries in address list to have retry cnt
     * greater than max retries and next reconnect time after some period */
    AddrList::iterator ai;
    for (ai = remote_addrs.begin(); ai != remote_addrs.end(); ++ai)
    {
        AddrEntry& ae(AddrList::get_value(ai));
        if (ae.get_uuid() == uuid)
        {
            log_info << "forgetting " << uuid
                     << " (" << AddrList::get_key(ai) << ")";

            ProtoMap::iterator pi, pi_next;
            for (pi = proto_map->begin(); pi != proto_map->end(); pi = pi_next)
            {
                pi_next = pi, ++pi_next;
                Proto* rp = ProtoMap::get_value(pi);
                if (rp->get_remote_addr() == AddrList::get_key(ai))
                {
                    log_info << "deleting entry " << AddrList::get_key(ai);
                    delete rp;
                    proto_map->erase(pi);
                }
            }
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            ae.set_next_reconnect(Date::now() + time_wait);
        }
    }

    /* Update state */
    update_addresses();
}

void GMCast::handle_connected(Proto* rp)
{
    const SocketPtr tp(rp->get_socket());
    assert(tp->get_state() == Socket::S_CONNECTED);
    log_debug << "transport " << tp << " connected";
    if (rp->get_state() == Proto::S_INIT)
    {
        log_debug << "sending hanshake";
        // accepted socket was waiting for underlying transport
        // handshake to finish
        rp->send_handshake();
    }
}

void GMCast::handle_established(Proto* est)
{
    log_debug << self_string() << " connection established to "
              << est->get_remote_uuid() << " "
              << est->get_remote_addr();

    if (est->get_remote_uuid() == get_uuid())
    {
        // connected to self
        if (est->get_remote_addr() == initial_addr)
        {
            proto_map->erase(
                proto_map->find_checked(est->get_socket()->get_id()));
            delete est;
            gu_throw_error(EINVAL)
                << "connected to own listening address '"
                <<  initial_addr
                << "', check that cluster address '"
                << uri_.get_host()
                << "' points to correct location";
        }
        else
        {
            AddrList::iterator i(pending_addrs.find(est->get_remote_addr()));
            if (i != pending_addrs.end())
            {
                log_warn << self_string()
                         << " address '" << est->get_remote_addr()
                         << "' points to own listening address, blacklisting";
                pending_addrs.erase(i);
                addr_blacklist.insert(make_pair(est->get_remote_addr(),
                                                AddrEntry(Date::now(),
                                                          Date::now(),
                                                          est->get_remote_uuid())));
            }
            proto_map->erase(
                proto_map->find_checked(est->get_socket()->get_id()));
            delete est;
            update_addresses();
        }
        return;
    }


    // If address is found from pending_addrs, move it to remote_addrs list
    // and set retry cnt to -1
    const string& remote_addr(est->get_remote_addr());
    AddrList::iterator i(pending_addrs.find(remote_addr));

    if (i != pending_addrs.end())
    {
        log_debug << "Erasing " << remote_addr << " from panding list";
        pending_addrs.erase(i);
    }

    if ((i = remote_addrs.find(remote_addr)) == remote_addrs.end())
    {
        log_debug << "Inserting " << remote_addr << " to remote list";

        insert_address (remote_addr, est->get_remote_uuid(), remote_addrs);
        i = remote_addrs.find(remote_addr);
    }
    else if (AddrList::get_value(i).get_uuid() != est->get_remote_uuid())
    {
        log_info << "remote endpoint " << est->get_remote_addr()
                 << " changed identity " << AddrList::get_value(i).get_uuid()
                 << " -> " << est->get_remote_uuid();
        remote_addrs.erase(i);
        i = remote_addrs.insert_unique(
            make_pair(est->get_remote_addr(),
                      AddrEntry(Date::now(),
                                Date::max(),
                                est->get_remote_uuid())));
    }

    if (AddrList::get_value(i).get_retry_cnt() >
        AddrList::get_value(i).get_max_retries())
    {
        log_warn << "discarding established (time wait) "
                 << est->get_remote_uuid()
                 << " (" << est->get_remote_addr() << ") ";
        proto_map->erase(proto_map->find(est->get_socket()->get_id()));
        delete est;
        update_addresses();
        return;
    }

    // send_up(Datagram(), p->get_remote_uuid());

    // init retry cnt to -1 to avoid unnecessary logging at first attempt
    // max retries will be readjusted in handle stable view
    AddrList::get_value(i).set_retry_cnt(-1);
    AddrList::get_value(i).set_max_retries(max_initial_reconnect_attempts);

    // Cleanup all previously established entries with same
    // remote uuid. It is assumed that the most recent connection
    // is usually the healthiest one.
    ProtoMap::iterator j, j_next;
    for (j = proto_map->begin(); j != proto_map->end(); j = j_next)
    {
        j_next = j, ++j_next;

        Proto* p(ProtoMap::get_value(j));

        if (p->get_remote_uuid() == est->get_remote_uuid())
        {
            if (p->get_handshake_uuid() < est->get_handshake_uuid())
            {
                log_info << self_string()
                          << " cleaning up duplicate "
                          << p->get_socket()
                          << " after established "
                          << est->get_socket();
                proto_map->erase(j);
                delete p;
            }
            else if (p->get_handshake_uuid() > est->get_handshake_uuid())
            {
                log_info << self_string()
                         << " cleaning up established "
                         << est->get_socket()
                         << " which is duplicate of "
                         << p->get_socket();
                proto_map->erase(
                    proto_map->find_checked(est->get_socket()->get_id()));
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

void GMCast::handle_failed(Proto* failed)
{
    log_debug << "handle failed: " << failed->get_socket();
    const string& remote_addr = failed->get_remote_addr();

    bool found_ok(false);
    for (ProtoMap::const_iterator i = proto_map->begin();
         i != proto_map->end(); ++i)
    {
        Proto* p(ProtoMap::get_value(i));
        if (p->get_state()       <= Proto::S_OK &&
            p->get_remote_uuid() == failed->get_remote_uuid())
        {
            found_ok = true;
            break;
        }
    }

    if (found_ok == false && remote_addr != "")
    {
        AddrList::iterator i;

        if ((i = pending_addrs.find(remote_addr)) != pending_addrs.end() ||
            (i = remote_addrs.find(remote_addr))  != remote_addrs.end())
        {
            AddrEntry& ae(AddrList::get_value(i));
            ae.set_retry_cnt(ae.get_retry_cnt() + 1);

            Date rtime = Date::now() + Period("PT1S");
            log_debug << self_string()
                      << " setting next reconnect time to "
                      << rtime << " for " << remote_addr;
            ae.set_next_reconnect(rtime);
        }
    }

    proto_map->erase(failed->get_socket()->get_id());
    delete failed;
    update_addresses();
}


bool GMCast::is_connected(const string& addr, const UUID& uuid) const
{
    for (ProtoMap::const_iterator i = proto_map->begin();
         i != proto_map->end(); ++i)
    {
        Proto* conn = ProtoMap::get_value(i);

        if (addr == conn->get_remote_addr() ||
            uuid == conn->get_remote_uuid())
        {
            return true;
        }
    }

    return false;
}


void GMCast::insert_address (const string& addr,
                             const UUID&   uuid,
                             AddrList&     alist)
{
    if (addr == listen_addr)
    {
        gu_throw_fatal << "Trying to add self addr " << addr << " to addr list";
    }

    if (alist.insert(make_pair(addr,
                               AddrEntry(Date::now(),
                                         Date::now(), uuid))).second == false)
    {
        log_warn << "Duplicate entry: " << addr;
    }
    else
    {
        log_debug << self_string() << ": new address entry " << uuid << ' '
                  << addr;
    }
}


void GMCast::update_addresses()
{
    LinkMap link_map;
    set<UUID> uuids;
    /* Add all established connections into uuid_map and update
     * list of remote addresses */

    ProtoMap::iterator i, i_next;
    for (i = proto_map->begin(); i != proto_map->end(); i = i_next)
    {
        i_next = i, ++i_next;

        Proto* rp = ProtoMap::get_value(i);

        if (rp->get_state() == Proto::S_OK)
        {
            if (rp->get_remote_addr() == "" ||
                rp->get_remote_uuid() == UUID::nil())
            {
                gu_throw_fatal << "Protocol error: local: (" << my_uuid
                               << ", '" << listen_addr
                               << "'), remote: (" << rp->get_remote_uuid()
                               << ", '" << rp->get_remote_addr() << "')";
            }

            if (remote_addrs.find(rp->get_remote_addr()) == remote_addrs.end())
            {
                log_warn << "Connection exists but no addr on addr list for "
                         << rp->get_remote_addr();
                insert_address(rp->get_remote_addr(), rp->get_remote_uuid(),
                               remote_addrs);
            }

            if (uuids.insert(rp->get_remote_uuid()).second == false)
            {
                // Duplicate entry, drop this one
                // @todo Deeper inspection about the connection states
                log_debug << self_string() << " dropping duplicate entry";
                proto_map->erase(i);
                delete rp;
            }
            else
            {
                link_map.insert(Link(rp->get_remote_uuid(),
                                     rp->get_remote_addr(),
                                     rp->get_mcast_addr()));
            }
        }
    }

    /* Send topology change message containing only established
     * connections */
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        Proto* gp = ProtoMap::get_value(i);

        // @todo: a lot of stuff here is done for each connection, including
        //        message creation and serialization. Need a mcast_msg() call
        //        and move this loop in there.
        if (gp->get_state() == Proto::S_OK)
            gp->send_topology_change(link_map);
    }

    /* Add entries reported by all other nodes to address list to
     * get complete view of existing uuids/addresses */
    for (ProtoMap::iterator i = proto_map->begin(); i != proto_map->end(); ++i)
    {
        Proto* rp = ProtoMap::get_value(i);

        if (rp->get_state() == Proto::S_OK)
        {
            for (LinkMap::const_iterator j = rp->get_link_map().begin();
                 j != rp->get_link_map().end(); ++j)
            {
                const UUID& link_uuid(LinkMap::get_key(j));
                const string& link_addr(LinkMap::get_value(j).get_addr());
                gcomm_assert(link_uuid != UUID::nil() && link_addr != "");

                if (addr_blacklist.find(link_addr) != addr_blacklist.end())
                {
                    log_info << self_string()
                             << " address '" << link_addr
                             << "' pointing to uuid " << link_uuid
                             << " is blacklisted, skipping";
                    continue;
                }

                if (link_uuid                     != get_uuid()         &&
                    remote_addrs.find(link_addr)  == remote_addrs.end() &&
                    pending_addrs.find(link_addr) == pending_addrs.end())
                {
                    log_debug << self_string()
                              << " conn refers to but no addr in addr list for "
                              << link_addr;
                    insert_address(link_addr, link_uuid, remote_addrs);

                    AddrList::iterator pi(remote_addrs.find(link_addr));

                    assert(pi != remote_addrs.end());

                    AddrEntry& ae(AddrList::get_value(pi));

                    // init retry cnt to -1 to avoid unnecessary logging
                    // at first attempt
                    // max retries will be readjusted in handle stable view
                    ae.set_retry_cnt(-1);
                    ae.set_max_retries(max_initial_reconnect_attempts);

                    // Add some randomness for first reconnect to avoid
                    // simultaneous connects
                    Date rtime(Date::now());

                    rtime = rtime + ::rand() % (100*MSec);
                    ae.set_next_reconnect(rtime);
                    next_check = min(next_check, rtime);
                }
            }
        }
    }

    // Build multicast tree
    log_debug << self_string() << " --- mcast tree begin ---";
    mcast_tree.clear();

    if (mcast != 0)
    {
        log_debug << mcast_addr;
        mcast_tree.push_back(mcast.get());
    }

    for (ProtoMap::const_iterator i(proto_map->begin()); i != proto_map->end();
         ++i)
    {
        const Proto& p(*ProtoMap::get_value(i));

        log_debug << "Proto: " << p.get_state() << " " << p.get_remote_addr()
                  << " " << p.get_mcast_addr();

        if (p.get_state() == Proto::S_OK &&
            (p.get_mcast_addr() == "" ||
             p.get_mcast_addr() != mcast_addr))
        {
            log_debug << p.get_remote_addr();
            mcast_tree.push_back(p.get_socket().get());
        }
    }
    log_debug << self_string() << " --- mcast tree end ---";
}


void GMCast::reconnect()
{
    /* Loop over known remote addresses and connect if proto entry
     * does not exist */
    Date now = Date::now();
    AddrList::iterator i, i_next;

    for (i = pending_addrs.begin(); i != pending_addrs.end(); i = i_next)
    {
        i_next = i, ++i_next;

        const string& pending_addr(AddrList::get_key(i));
        const AddrEntry& ae(AddrList::get_value(i));

        if (is_connected (pending_addr, UUID::nil()) == false &&
            ae.get_next_reconnect()                  <= now)
        {
            if (ae.get_retry_cnt() > ae.get_max_retries())
            {
                log_info << "cleaning up pending addr " << pending_addr;
                pending_addrs.erase(i);
                continue; // no reference to pending_addr after this
            }
            else if (ae.get_next_reconnect() <= now)
            {
                // log_debug << "Connecting to " << pending_addr;
                gmcast_connect (pending_addr);
            }
        }
    }


    for (i = remote_addrs.begin(); i != remote_addrs.end(); i = i_next)
    {
        i_next = i, ++i_next;

        const string& remote_addr(AddrList::get_key(i));
        const AddrEntry& ae(AddrList::get_value(i));
        const UUID& remote_uuid(ae.get_uuid());

        gcomm_assert(remote_uuid != get_uuid());

        if (is_connected(remote_addr, remote_uuid) == false &&
            ae.get_next_reconnect()                <= now)
        {
            if (ae.get_retry_cnt() > ae.get_max_retries())
            {
                log_info << " cleaning up " << remote_uuid << " ("
                         << remote_addr << ")";
                remote_addrs.erase(i);
                continue;//no reference to remote_addr or remote_uuid after this
            }
            else if (ae.get_next_reconnect() <= now)
            {
                if (ae.get_retry_cnt() % 30 == 0)
                {
                    log_info << self_string() << " reconnecting to "
                             << remote_uuid << " (" << remote_addr
                             << "), attempt " << ae.get_retry_cnt();
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


void gcomm::GMCast::check_liveness()
{
    std::set<UUID> live_uuids;

    // iterate over proto map and mark all timed out entries as failed
    gu::datetime::Date now(gu::datetime::Date::now());
    for (ProtoMap::iterator i(proto_map->begin()); i != proto_map->end(); )
    {
        ProtoMap::iterator i_next(i);
        ++i_next;
        Proto* p(ProtoMap::get_value(i));
        if (p->get_state() > Proto::S_INIT &&
            p->get_state() < Proto::S_FAILED &&
            p->get_tstamp() + peer_timeout < now)
        {
            log_debug << self_string()
                      << " connection to peer "
                      << p->get_remote_uuid() << " with addr "
                      << p->get_remote_addr()
                      << " timed out";
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
        else if (p->get_state() == Proto::S_OK)
        {
            // log_info << "live proto " << *p;
            live_uuids.insert(p->get_remote_uuid());
        }
        i = i_next;
    }

    bool should_relay(false);

    // iterate over addr list and check if there is at least one live
    // proto entry associated to each addr entry

    std::string nonlive_peers;
    for (AddrList::const_iterator i(remote_addrs.begin());
         i != remote_addrs.end(); ++i)
    {
        const AddrEntry& ae(AddrList::get_value(i));
        if (ae.get_retry_cnt()             <= ae.get_max_retries() &&
            live_uuids.find(ae.get_uuid()) == live_uuids.end())
        {
            // log_info << self_string()
            // << " missing live proto entry for " << ae.get_uuid();
            nonlive_peers += AddrList::get_key(i) + " ";
            should_relay = true;
        }
    }

    if (relaying == false && should_relay == true)
    {
        log_info << self_string()
                 << " turning message relay requesting on, nonlive peers: "
                 << nonlive_peers;
        relaying = true;
    }
    else if (relaying == true && should_relay == false)
    {
        log_info << self_string() << " turning message relay requesting off";
        relaying = false;
    }

}


Date gcomm::GMCast::handle_timers()
{
    const Date now(Date::now());

    if (now >= next_check)
    {
        check_liveness();
        reconnect();
        next_check = now + check_period;
    }

    return next_check;
}


void gcomm::GMCast::relay(const Message& msg, const Datagram& dg,
                          const void* exclude_id)
{
    Message relay_msg(msg);
    relay_msg.set_flags(relay_msg.get_flags() & ~Message::F_RELAY);
    Datagram relay_dg(dg);
    relay_dg.normalize();
    gu_trace(push_header(relay_msg, relay_dg));
    for (list<Socket*>::iterator i(mcast_tree.begin());
         i != mcast_tree.end(); ++i)
    {
        int err;
        if ((*i)->get_id() != exclude_id &&
            (err = (*i)->send(relay_dg)) != 0)
        {
            log_debug << "transport: " << ::strerror(err);
        }
    }
}

void GMCast::handle_up(const void*        id,
                       const Datagram&    dg,
                       const ProtoUpMeta& um)
{
    ProtoMap::iterator i;

    if (listener == 0) { return; }

    if (id == listener->get_id())
    {
        gmcast_accept();
    }
    else if (mcast.get() != 0 && id == mcast->get_id())
    {
        Message msg;

        try
        {
            if (dg.get_offset() < dg.get_header_len())
            {
                gu_trace(msg.unserialize(dg.get_header(), dg.get_header_size(),
                                         dg.get_header_offset() +
                                         dg.get_offset()));
            }
            else
            {
                gu_trace(msg.unserialize(&dg.get_payload()[0],
                                         dg.get_len(),
                                         dg.get_offset()));
            }
        }
        catch (Exception& e)
        {
            GU_TRACE(e);
            log_warn << e.what();
            return;
        }

        if (msg.get_type() >= Message::T_USER_BASE)
        {
            gu_trace(send_up(Datagram(dg, dg.get_offset() + msg.serial_size()),
                             ProtoUpMeta(msg.get_source_uuid())));
        }
        else
        {
            log_warn << "non-user message " << msg.get_type()
                     << " from multicast socket";
        }
    }
    else if ((i = proto_map->find(id)) != proto_map->end())
    {
        Proto* p(ProtoMap::get_value(i));

        if (dg.get_len() > 0)
        {
            const Proto::State prev_state(p->get_state());

            if (prev_state == Proto::S_FAILED)
            {
                log_warn << "unhandled failed proto";
                handle_failed(p);
                return;
            }

            Message msg;

            try
            {
                msg.unserialize(&dg.get_payload()[0], dg.get_len(),
                                dg.get_offset());
            }
            catch (Exception& e)
            {
                GU_TRACE(e);
                log_warn << e.what();
                p->set_state(Proto::S_FAILED);
                handle_failed(p);
                return;
            }

            if (msg.get_type() >= Message::T_USER_BASE)
            {
                if (msg.get_flags() & Message::F_RELAY)
                {
                    relay(msg,
                          Datagram(dg, dg.get_offset() + msg.serial_size()),
                          id);
                }
                send_up(Datagram(dg, dg.get_offset() + msg.serial_size()),
                        ProtoUpMeta(msg.get_source_uuid()));
                p->set_tstamp(gu::datetime::Date::now());
            }
            else
            {
                try
                {
                    gu_trace(p->handle_message(msg));
                    p->set_tstamp(gu::datetime::Date::now());
                }
                catch (Exception& e)
                {
                    log_warn << "handling gmcast protocol message failed: "
                             << e.what();
                    handle_failed(p);
                    return;
                }

                if (p->get_state() == Proto::S_FAILED)
                {
                    handle_failed(p);
                    return;
                }
                else if (p->get_changed() == true)
                {
                    update_addresses();
                    check_liveness();
                    reconnect();
                }
            }

            if (prev_state != Proto::S_OK && p->get_state() == Proto::S_OK)
            {
                handle_established(p);
            }
        }
        else if (p->get_socket()->get_state() == Socket::S_CONNECTED &&
                 (p->get_state() == Proto::S_HANDSHAKE_WAIT ||
                  p->get_state() == Proto::S_INIT))
        {
            handle_connected(p);
        }
        else if (p->get_socket()->get_state() == Socket::S_CONNECTED)
        {
            log_warn << "connection " << p->get_socket()->get_id()
                     << " closed by peer";
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
        else
        {
            log_debug << "socket in state " << p->get_socket()->get_state();
            p->set_state(Proto::S_FAILED);
            handle_failed(p);
        }
    }
    else
    {
        // log_info << "proto entry " << id << " not found";
    }
}

int GMCast::handle_down(Datagram& dg, const ProtoDownMeta& dm)
{
    Message msg(version, Message::T_USER_BASE, get_uuid(), 1);

    gu_trace(push_header(msg, dg));

    size_t relay_idx(mcast_tree.size());
    if (relaying == true && relay_idx > 0)
    {
        relay_idx = rand() % relay_idx;
    }

    size_t idx(0);
    for (list<Socket*>::iterator i(mcast_tree.begin());
         i != mcast_tree.end(); ++i, ++idx)
    {
        if (relay_idx == idx)
        {
            gu_trace(pop_header(msg, dg));
            msg.set_flags(msg.get_flags() | Message::F_RELAY);
            gu_trace(push_header(msg, dg));
        }
        int err;
        if ((err = (*i)->send(dg)) != 0)
        {
            log_debug << "transport: " << ::strerror(err);
        }
        if (relay_idx == idx)
        {
            gu_trace(pop_header(msg, dg));
            msg.set_flags(msg.get_flags() & ~Message::F_RELAY);
            gu_trace(push_header(msg, dg));
        }
    }

    gu_trace(pop_header(msg, dg));

    return 0;
}

void gcomm::GMCast::handle_stable_view(const View& view)
{
    log_debug << "GMCast::handle_stable_view: " << view;
    if (view.get_type() == V_PRIM)
    {
        std::set<UUID> gmcast_lst;
        for (AddrList::const_iterator i(remote_addrs.begin());
             i != remote_addrs.end(); ++i)
        {
            gmcast_lst.insert(i->second.get_uuid());
        }
        std::set<UUID> view_lst;
        for (NodeList::const_iterator i(view.get_members().begin());
             i != view.get_members().end(); ++i)
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
            if ((ai = find_if(remote_addrs.begin(), remote_addrs.end(),
                              AddrListUUIDCmp(*i))) != remote_addrs.end())
            {
                ai->second.set_retry_cnt(-1);
                ai->second.set_max_retries(max_retry_cnt);
            }
        }
    }
    else if (view.get_type() == V_REG)
    {
        for (NodeList::const_iterator i(view.get_members().begin());
             i != view.get_members().end(); ++i)
        {
            AddrList::iterator ai;
            if ((ai = find_if(remote_addrs.begin(), remote_addrs.end(),
                              AddrListUUIDCmp(NodeList::get_key(i))))
                != remote_addrs.end())
            {
                log_info << "declaring " << NodeList::get_key(i) << " stable";
                ai->second.set_retry_cnt(-1);
                ai->second.set_max_retries(max_retry_cnt);
            }
        }
    }
    check_liveness();
}

void gcomm::GMCast::add_or_del_addr(const std::string& val)
{
    if (val.compare(0, 4, "add:") == 0)
    {
        gu::URI uri(val.substr(4));
        std::string addr(resolve(uri_string(get_scheme(use_ssl),
                                            uri.get_host(),
                                            uri.get_port())).to_string());
        log_info << "inserting address '" << addr << "'";
        insert_address(addr, UUID(), remote_addrs);
        AddrList::iterator ai(remote_addrs.find(addr));
        AddrList::get_value(ai).set_max_retries(
            max_initial_reconnect_attempts);
        AddrList::get_value(ai).set_retry_cnt(-1);
    }
    else if (val.compare(0, 4, "del:") == 0)
    {
        std::string addr(val.substr(4));
        AddrList::iterator ai(remote_addrs.find(addr));
        if (ai != remote_addrs.end())
        {
            ProtoMap::iterator pi, pi_next;
            for (pi = proto_map->begin(); pi != proto_map->end(); pi = pi_next)
            {
                pi_next = pi, ++pi_next;
                Proto* rp = ProtoMap::get_value(pi);
                if (rp->get_remote_addr() == AddrList::get_key(ai))
                {
                    log_info << "deleting entry " << AddrList::get_key(ai);
                    delete rp;
                    proto_map->erase(pi);
                }
            }
            AddrEntry& ae(AddrList::get_value(ai));
            ae.set_max_retries(0);
            ae.set_retry_cnt(1);
            ae.set_next_reconnect(Date::now() + time_wait);
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
        max_initial_reconnect_attempts = gu::from_string<int>(val);
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
    return false;
}
