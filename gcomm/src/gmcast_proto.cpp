/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#include "gmcast_proto.hpp"
#include "gmcast.hpp"

#include "gu_uri.hpp"

static const std::string gmcast_proto_err_evicted("evicted");
static const std::string gmcast_proto_err_invalid_group("invalid group");
static const std::string gmcast_proto_err_duplicate_uuid("duplicate uuid");

const gcomm::UUID& gcomm::gmcast::Proto::local_uuid() const
{
    return gmcast_.uuid();
}

std::ostream& gcomm::gmcast::operator<<(std::ostream& os, const Proto& p)
{
    os << "v="  << p.version_ << ","
       << "hu=" << p.handshake_uuid_ << ","
       << "lu=" << p.gmcast_.uuid() << ","
       << "ru=" << p.remote_uuid_ << ","
       << "ls=" << static_cast<int>(p.local_segment_) << ","
       << "rs=" << static_cast<int>(p.remote_segment_) << ","
       << "la=" << p.local_addr_ << ","
       << "ra=" << p.remote_addr_ << ","
       << "mc=" << p.mcast_addr_ << ","
       << "gn=" << p.group_name_ << ","
       << "ch=" << p.changed_ << ","
       << "st=" << gcomm::gmcast::Proto::to_string(p.state_) << ","
       << "pr=" << p.propagate_remote_ << ","
       << "tp=" << p.tp_ << ","
       << "rts=" << p.recv_tstamp_ << ","
       << "sts=" << p.send_tstamp_;
    return os;
}


void gcomm::gmcast::Proto:: set_state(State new_state)
{
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

    log_debug << *this << " from state: " << to_string(state_)
              << " to state: " << to_string(new_state);

    state_ = new_state;
}

void gcomm::gmcast::Proto::send_msg(const Message& msg,
                                    bool ignore_no_buffer_space)
{
    gu::Buffer buf;
    gu_trace(serialize(msg, buf));
    Datagram dg(buf);
    int ret = tp_->send(msg.segment_id(), dg);

    if (ret != 0)
    {
        if (not (ret == ENOBUFS && ignore_no_buffer_space))
        {
            log_debug << "Send failed: " << strerror(ret);
            set_state(S_FAILED);
        }
    }
}

void gcomm::gmcast::Proto::send_handshake()
{
    handshake_uuid_ = UUID(0, 0);
    Message hs (version_, Message::GMCAST_T_HANDSHAKE, handshake_uuid_,
                gmcast_.uuid(), local_segment_);

    send_msg(hs, false);

    set_state(S_HANDSHAKE_SENT);
}

void gcomm::gmcast::Proto::wait_handshake()
{
    if (state() != S_INIT)
        gu_throw_fatal << "Invalid state: " << to_string(state());

    set_state(S_HANDSHAKE_WAIT);
}

bool gcomm::gmcast::Proto::validate_handshake_uuid()
{

    //
    // Sanity checks for duplicate UUIDs.
    //
    // 1) Check if the other endpoint exists on this node. If so,
    //    the address will be blacklisted and this connection terminated.
    // 2) Check if the remote endpoint has same UUID and abort if
    //    this node has not reached prim view. This deals with the case where
    //    this node is connected to the node with same UUID and this node
    //    has not reached primary component yet.
    // 3) This node is connected to an another node which has an
    //    UUID which already exists in the cluster with different
    //    address. This may happen if
    //    - The other node has restarted fast and regenerated a new UUID
    //      which conflicts with existing UUID
    //    - The other node changed its address
    //    In this case we send an evict message and rely on the other
    //    node to take correct action (abort if it was joining, retry
    //    if its address changed).
    //
    if (gmcast_.is_own(this))
    {
        // Connecting to own address should not get past the first
        // handshake message so we should see here only S_HANDSHAKE_WAIT
        // state.
        assert(state() == S_HANDSHAKE_WAIT);
        log_info << gmcast_.self_string()
                 << " Found matching local endpoint for a connection, "
                 << "blacklisting address " << remote_addr();
        gmcast_.blacklist(this);
        set_state(S_FAILED);
        return false;
    }
    else if (gmcast_.uuid() == remote_uuid() &&
             gmcast_.prim_view_reached() == false)
    {
        // Direct connection to node with the same UUID, the duplicate
        // UUID should be handled when the first handshake message
        // is seen, so we should see here only S_HANDSHAKE_WAIT state.
        assert(state() == S_HANDSHAKE_WAIT);
        // Remove gvwstate.dat, otherwise the same UUID will be
        // used again when the node is restarted.
        gmcast_.remove_viewstate_file();
        set_state(S_FAILED);
        gu_throw_fatal
            << "A node with the same UUID already exists in the cluster. "
            << "Removing gvwstate.dat file, this node will generate a new "
            << "UUID when restarted.";
    }
    else if (gmcast_.is_not_own_and_duplicate_exists(this))
    {
        evict_duplicate_uuid(); // Sets state to failed
        return false;
    }
    return true;
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

    if (validate_handshake_uuid() == false)
    {
        assert(state() == S_FAILED); // Should be adjusted by validate
        return;
    }

    Message hsr (version_, Message::GMCAST_T_HANDSHAKE_RESPONSE,
                 handshake_uuid_,
                 gmcast_.uuid(),
                 local_addr_,
                 group_name_,
                 local_segment_);
    send_msg(hsr, false);

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
            Message failed(version_, Message::GMCAST_T_FAIL,
                           gmcast_.uuid(), local_segment_,
                           gmcast_proto_err_invalid_group);
            send_msg(failed, false);
            set_state(S_FAILED);
            return;
        }
        remote_uuid_ = hs.source_uuid();
        remote_segment_ = hs.segment_id();
        gu::URI remote_uri(tp_->remote_addr());
        remote_addr_ = uri_string(remote_uri.get_scheme(),
                                  remote_uri.get_host(),
                                  gu::URI(hs.node_address()).get_port());


        if (gmcast_.is_evicted(remote_uuid_) == true)
        {
            log_info << "peer " << remote_uuid_
                     << " from " << remote_addr_
                     << " has been evicted out, rejecting connection";
            evict();
            return;
        }

        if (validate_handshake_uuid() == false)
        {
            assert(state() == S_FAILED); // Should be adjusted by validate
            return;
        }

        propagate_remote_ = true;
        Message ok(version_, Message::GMCAST_T_OK, gmcast_.uuid(),
                   local_segment_, "");
        send_msg(ok, false);
        set_state(S_OK);
    }
    catch (std::exception& e)
    {
        log_warn << "Parsing peer address '"
                 << hs.node_address() << "' failed: " << e.what();
        Message nok (version_, Message::GMCAST_T_FAIL,
                     gmcast_.uuid(), local_segment_,
                     "invalid node address");
        send_msg (nok, false);
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
    log_warn << "handshake with " << remote_uuid_ << " "
             << remote_addr_ << " failed: '"
             << hs.error() << "'";
    set_state(S_FAILED);
    if (hs.error() == gmcast_proto_err_evicted)
    {
        // otherwise node use the uuid in view state file.
        // which is probably still in other nodes evict list.
        gmcast_.remove_viewstate_file();
        gu_throw_fatal
            << "this node has been evicted out of the cluster, "
            << "gcomm backend restart is required";
    }
    else if (hs.error() == gmcast_proto_err_duplicate_uuid)
    {
        if (gmcast_.prim_view_reached())
        {
            log_warn << "Received duplicate UUID error from other node "
                     << "while in primary component. This may mean that "
                     << "this node's IP address has changed. Will close "
                     << "connection and keep on retrying";
        }
        else
        {
            // Remove gvwstate.dat, otherwise the same UUID will be
            // used again when the node is restarted.
            gmcast_.remove_viewstate_file();
            gu_throw_fatal
                << "A node with the same UUID already exists in the cluster. "
                << "Removing gvwstate.dat file, this node will generate a new "
                << "UUID when restarted.";
        }
    }
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
    using std::rel_ops::operator!=;
    if (link_map_ != new_map)
    {
        changed_ = true;
    }
    link_map_ = new_map;
}

void gcomm::gmcast::Proto::handle_keepalive(const Message& msg)
{
    log_debug << "keepalive: " << *this;
    Message ok(version_, Message::GMCAST_T_OK, gmcast_.uuid(), local_segment_, "");
    send_msg(ok, true);
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

    Message msg(version_, Message::GMCAST_T_TOPOLOGY_CHANGE, gmcast_.uuid(),
                group_name_, nl);

    send_msg(msg, false);
}


void gcomm::gmcast::Proto::send_keepalive()
{
    log_debug << "sending keepalive: " << *this;
    Message msg(version_, Message::GMCAST_T_KEEPALIVE,
                gmcast_.uuid(), local_segment_, "");
    send_msg(msg, true);
}

void gcomm::gmcast::Proto::evict()
{
    Message failed(version_, Message::GMCAST_T_FAIL,
                   gmcast_.uuid(), local_segment_, gmcast_proto_err_evicted);
    send_msg(failed, false);
    set_state(S_FAILED);
}

void gcomm::gmcast::Proto::evict_duplicate_uuid()
{
    Message failed(version_, Message::GMCAST_T_FAIL,
                   gmcast_.uuid(), local_segment_,
                   gmcast_proto_err_duplicate_uuid);
    send_msg(failed, false);
    set_state(S_FAILED);
}

void gcomm::gmcast::Proto::handle_message(const Message& msg)
{

    switch (msg.type())
    {
    case Message::GMCAST_T_HANDSHAKE:
        handle_handshake(msg);
        break;
    case Message::GMCAST_T_HANDSHAKE_RESPONSE:
        handle_handshake_response(msg);
        break;
    case Message::GMCAST_T_OK:
        handle_ok(msg);
        break;
    case Message::GMCAST_T_FAIL:
        handle_failed(msg);
        break;
    case Message::GMCAST_T_TOPOLOGY_CHANGE:
        handle_topology_change(msg);
        break;
    case Message::GMCAST_T_KEEPALIVE:
        handle_keepalive(msg);
        break;
    default:
        gu_throw_fatal << "invalid message type: " << msg.type();
    }
}
