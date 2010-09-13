/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gmcast_proto.hpp"

#include "gu_uri.hpp"

using namespace std;
using namespace std::rel_ops;
using namespace gu;
using namespace gu::net;
using namespace gcomm;

void gcomm::gmcast::Proto:: set_state(State new_state)
{
    log_debug << "State change: " << to_string(state) << " -> "
              << to_string(new_state);

    static const bool allowed[][7] =
        {
            // INIT  HS_SENT HS_WAIT HSR_SENT   OK    FAILED CLOSED
            { false,  true,   true,   false,  false,  false, false },// INIT

            { false,  false,  false,  false,  true,   true,  false },// HS_SENT

            { false,  false,  false,  true,   false,  true, false },// HS_WAIT

            { false,  false,  false,  false,  true,   true,  false },// HSR_SENT

            { false,  false,  false,  false,  false,  true,  true  },// OK

            { false,  false,  false,  false,  false,  true, true  },// FAILED

            { false,  false,  false,  false,  false,  false, false } // CLOSED
        };

    if (!allowed[state][new_state])
    {
        gu_throw_fatal << "Invalid state change: " << to_string(state)
                          << " -> " << to_string(new_state);
    }

    state = new_state;
}

void gcomm::gmcast::Proto::send_msg(const Message& msg)
{
    gu::Buffer buf;
    gu_trace(serialize(msg, buf));
    Datagram dg(buf);
    int ret = tp->send(dg);

    // @todo: This can happen during congestion, figure out how to
    // avoid terminating connection with topology change messages.
    if (ret != 0)
    {
        log_debug << "Send failed: " << strerror(ret);
        set_state(S_FAILED);
    }
}

void gcomm::gmcast::Proto::send_handshake()
{
    handshake_uuid = UUID(0, 0);
    Message hs (version, Message::T_HANDSHAKE, handshake_uuid, local_uuid);

    send_msg(hs);

    set_state(S_HANDSHAKE_SENT);
}

void gcomm::gmcast::Proto::wait_handshake()
{
    if (get_state() != S_INIT)
        gu_throw_fatal << "Invalid state: " << to_string(get_state());

    set_state(S_HANDSHAKE_WAIT);
}

void gcomm::gmcast::Proto::handle_handshake(const Message& hs)
{
    if (get_state() != S_HANDSHAKE_WAIT)
        gu_throw_fatal << "Invalid state: " << to_string(get_state());

    if (hs.get_version() != version)
    {
        log_warn << "incompatible protocol version: " << hs.get_version();
        set_state(S_FAILED);
        return;
    }
    handshake_uuid = hs.get_handshake_uuid();
    remote_uuid = hs.get_source_uuid();

    Message hsr (version, Message::T_HANDSHAKE_RESPONSE,
                 handshake_uuid,
                 local_uuid,
                 local_addr,
                 group_name);
    send_msg(hsr);

    set_state(S_HANDSHAKE_RESPONSE_SENT);
}

void gcomm::gmcast::Proto::handle_handshake_response(const Message& hs)
{
    if (get_state() != S_HANDSHAKE_SENT)
        gu_throw_fatal << "Invalid state: " << to_string(get_state());

        const std::string& grp = hs.get_group_name();

        try
        {
            if (grp != group_name)
            {
                log_info << "handshake failed, my group: '" << group_name
                         << "', peer group: '" << grp << "'";
                Message failed(version, Message::T_HANDSHAKE_FAIL,
                               handshake_uuid, local_uuid);
                send_msg(failed);
                set_state(S_FAILED);
                return;
            }
            remote_uuid = hs.get_source_uuid();
            gu::URI remote_uri(tp->get_remote_addr());
            remote_addr = remote_uri.get_scheme() + "://" + remote_uri.get_host() + ":"
                + URI(hs.get_node_address()).get_port();

            propagate_remote = true;
            Message ok(version, Message::T_HANDSHAKE_OK, handshake_uuid, local_uuid);
            send_msg(ok);
            set_state(S_OK);
        }
        catch (exception& e)
        {
            log_warn << "Parsing peer address '"
                     << hs.get_node_address() << "' failed: " << e.what();

            Message nok (version, Message::T_HANDSHAKE_FAIL, handshake_uuid, local_uuid);

            send_msg (nok);
            set_state(S_FAILED);
        }
}

void gcomm::gmcast::Proto::handle_ok(const Message& hs)
{
    propagate_remote = true;
    set_state(S_OK);
}

void gcomm::gmcast::Proto::handle_failed(const Message& hs)
{
    set_state(S_FAILED);
}


void gcomm::gmcast::Proto::handle_topology_change(const Message& msg)
{
    const Message::NodeList& nl(msg.get_node_list());

    LinkMap new_map;
    for (Message::NodeList::const_iterator i = nl.begin(); i != nl.end(); ++i)
    {
        new_map.insert(Link(Message::NodeList::get_key(i),
                            Message::NodeList::get_value(i).get_addr(),
                            Message::NodeList::get_value(i).get_mcast_addr()));
        if (Message::NodeList::get_key(i) == get_remote_uuid()     &&
            mcast_addr == "" &&
            Message::NodeList::get_value(i).get_mcast_addr() != "")
        {
            mcast_addr = Message::NodeList::get_value(i).get_mcast_addr();
        }
    }

    if (link_map != new_map)
    {
        changed = true;
    }
    link_map = new_map;
}


void gcomm::gmcast::Proto::send_topology_change(LinkMap& um)
{
    Message::NodeList nl;
    for (LinkMap::const_iterator i = um.begin(); i != um.end(); ++i)
    {
        if (LinkMap::get_key(i) == UUID::nil() ||
            LinkMap::get_value(i).get_addr() == "")
            gu_throw_fatal << "nil uuid or empty address";

        nl.insert_unique(make_pair(LinkMap::get_key(i),
                                   Node(LinkMap::get_value(i).get_addr())));
    }

    Message msg(version, Message::T_TOPOLOGY_CHANGE, local_uuid,
                group_name, nl);

    send_msg(msg);
}


void gcomm::gmcast::Proto::handle_message(const Message& msg)
{

    switch (msg.get_type())
    {
    case Message::T_HANDSHAKE:
        handle_handshake(msg);
        break;
    case Message::T_HANDSHAKE_RESPONSE:
        handle_handshake_response(msg);
        break;
    case Message::T_HANDSHAKE_OK:
        handle_ok(msg);
        break;
    case Message::T_HANDSHAKE_FAIL:
        handle_failed(msg);
        break;
    case Message::T_TOPOLOGY_CHANGE:
        handle_topology_change(msg);
        break;
    default:
        gu_throw_fatal << "invalid message type: " << msg.get_type();
        throw;
    }
}
