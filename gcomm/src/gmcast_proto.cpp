/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "gmcast_proto.hpp"

#include "gu_uri.hpp"

using std::rel_ops::operator!=;

void gcomm::gmcast::Proto:: set_state(State new_state)
{
    log_debug << "State change: " << to_string(state_) << " -> "
              << to_string(new_state);

    static const bool allowed[][7] =
        {
            // INIT  HS_SENT HS_WAIT HSR_SENT   OK    FAILED CLOSED
            { false,  true,   true,   false,  false,  true, false },// INIT

            { false,  false,  false,  false,  true,   true,  false },// HS_SENT

            { false,  false,  false,  true,   false,  true, false },// HS_WAIT

            { false,  false,  false,  false,  true,   true,  false },// HSR_SENT

            { false,  false,  false,  false,  true,   true,  true  },// OK

            { false,  false,  false,  false,  false,  true, true  },// FAILED

            { false,  false,  false,  false,  false,  false, false } // CLOSED
        };

    if (!allowed[state_][new_state])
    {
        gu_throw_fatal << "Invalid state change: " << to_string(state_)
                          << " -> " << to_string(new_state);
    }

    state_ = new_state;
}

void gcomm::gmcast::Proto::send_msg(const Message& msg)
{
    gu::Buffer buf;
    gu_trace(serialize(msg, buf));
    Datagram dg(buf);
    int ret = tp_->send(dg);

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
    handshake_uuid_ = UUID(0, 0);
    Message hs (version_, Message::T_HANDSHAKE, handshake_uuid_, local_uuid_,
                local_segment_);

    send_msg(hs);

    set_state(S_HANDSHAKE_SENT);
}

void gcomm::gmcast::Proto::wait_handshake()
{
    if (state() != S_INIT)
        gu_throw_fatal << "Invalid state: " << to_string(state());

    set_state(S_HANDSHAKE_WAIT);
}

void gcomm::gmcast::Proto::handle_handshake(const Message& hs)
{
    if (state() != S_HANDSHAKE_WAIT)
        gu_throw_fatal << "Invalid state: " << to_string(state());

    if (hs.version() != version_)
    {
        log_warn << "incompatible protocol version: " << hs.version();
        set_state(S_FAILED);
        return;
    }
    handshake_uuid_ = hs.handshake_uuid();
    remote_uuid_ = hs.source_uuid();
    remote_segment_ = hs.segment_id();

    Message hsr (version_, Message::T_HANDSHAKE_RESPONSE,
                 handshake_uuid_,
                 local_uuid_,
                 local_addr_,
                 group_name_,
                 local_segment_);
    send_msg(hsr);

    set_state(S_HANDSHAKE_RESPONSE_SENT);
}

void gcomm::gmcast::Proto::handle_handshake_response(const Message& hs)
{
    if (state() != S_HANDSHAKE_SENT)
        gu_throw_fatal << "Invalid state: " << to_string(state());

        const std::string& grp = hs.group_name();

        try
        {
            if (grp != group_name_)
            {
                log_info << "handshake failed, my group: '" << group_name_
                         << "', peer group: '" << grp << "'";
                Message failed(version_, Message::T_FAIL,
                               local_uuid_, local_segment_);
                send_msg(failed);
                set_state(S_FAILED);
                return;
            }
            remote_uuid_ = hs.source_uuid();
            remote_segment_ = hs.segment_id();
            gu::URI remote_uri(tp_->remote_addr());
            remote_addr_ = uri_string(remote_uri.get_scheme(),
                                      remote_uri.get_host(),
                                      gu::URI(hs.node_address()).get_port());

            propagate_remote_ = true;
            Message ok(version_, Message::T_OK, local_uuid_,
                       local_segment_);
            send_msg(ok);
            set_state(S_OK);
        }
        catch (std::exception& e)
        {
            log_warn << "Parsing peer address '"
                     << hs.node_address() << "' failed: " << e.what();

            Message nok (version_, Message::T_FAIL,
                         local_uuid_, local_segment_);

            send_msg (nok);
            set_state(S_FAILED);
        }
}

void gcomm::gmcast::Proto::handle_ok(const Message& hs)
{
    if (state_ == S_OK)
    {
        log_debug << "handshake ok: " << *this;
    }
    propagate_remote_ = true;
    set_state(S_OK);
}

void gcomm::gmcast::Proto::handle_failed(const Message& hs)
{
    set_state(S_FAILED);
}


void gcomm::gmcast::Proto::handle_topology_change(const Message& msg)
{
    const Message::NodeList& nl(msg.node_list());

    LinkMap new_map;
    for (Message::NodeList::const_iterator i = nl.begin(); i != nl.end(); ++i)
    {
        new_map.insert(Link(Message::NodeList::key(i),
                            Message::NodeList::value(i).addr(),
                            Message::NodeList::value(i).mcast_addr()));
        if (Message::NodeList::key(i) == remote_uuid()     &&
            mcast_addr_ == "" &&
            Message::NodeList::value(i).mcast_addr() != "")
        {
            mcast_addr_ = Message::NodeList::value(i).mcast_addr();
        }
    }

    if (link_map_ != new_map)
    {
        changed_ = true;
    }
    link_map_ = new_map;
}

void gcomm::gmcast::Proto::handle_keepalive(const Message& msg)
{
    log_debug << "keepalive: " << *this;
    Message ok(version_, Message::T_OK, local_uuid_, local_segment_);
    send_msg(ok);
}

void gcomm::gmcast::Proto::send_topology_change(LinkMap& um)
{
    Message::NodeList nl;
    for (LinkMap::const_iterator i = um.begin(); i != um.end(); ++i)
    {
        if (LinkMap::key(i) == UUID::nil() ||
            LinkMap::value(i).addr() == "")
            gu_throw_fatal << "nil uuid or empty address";

        nl.insert_unique(
            std::make_pair(LinkMap::key(i),
                           Node(LinkMap::value(i).addr())));
    }

    Message msg(version_, Message::T_TOPOLOGY_CHANGE, local_uuid_,
                group_name_, nl);

    send_msg(msg);
}


void gcomm::gmcast::Proto::send_keepalive()
{
    log_debug << "sending keepalive: " << *this;
    Message msg(version_, Message::T_KEEPALIVE,
                local_uuid_, local_segment_);
    send_msg(msg);
}

void gcomm::gmcast::Proto::handle_message(const Message& msg)
{

    switch (msg.type())
    {
    case Message::T_HANDSHAKE:
        handle_handshake(msg);
        break;
    case Message::T_HANDSHAKE_RESPONSE:
        handle_handshake_response(msg);
        break;
    case Message::T_OK:
        handle_ok(msg);
        break;
    case Message::T_FAIL:
        handle_failed(msg);
        break;
    case Message::T_TOPOLOGY_CHANGE:
        handle_topology_change(msg);
        break;
    case Message::T_KEEPALIVE:
        handle_keepalive(msg);
        break;
    default:
        gu_throw_fatal << "invalid message type: " << msg.type();
    }
}

