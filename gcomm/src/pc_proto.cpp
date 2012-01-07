/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "pc_proto.hpp"
#include "pc_message.hpp"

#include "gcomm/util.hpp"

#include "gu_logger.hpp"
#include "gu_macros.h"
#include <set>

using namespace std;
using namespace std::rel_ops;

using namespace gu;

using namespace gcomm;

//
// Helpers
//

class ToSeqCmpOp
{
public:
    bool operator()(const gcomm::pc::Proto::SMMap::value_type& a,
                    const gcomm::pc::Proto::SMMap::value_type& b) const
    {
        const gcomm::pc::Node& astate(
            gcomm::pc::NodeMap::get_value(
                gcomm::pc::Proto::SMMap::get_value(a).get_node_map()
                .find_checked(gcomm::pc::Proto::SMMap::get_key(a))));

        const gcomm::pc::Node& bstate(
            gcomm::pc::NodeMap::get_value(
                gcomm::pc::Proto::SMMap::get_value(b).get_node_map()
                .find_checked(gcomm::pc::Proto::SMMap::get_key(b))));

        return (astate.get_to_seq() < bstate.get_to_seq());
    }
};


static int64_t get_max_to_seq(const gcomm::pc::Proto::SMMap& states)
{
    gcomm_assert(states.empty() == false);
    gcomm::pc::Proto::SMMap::const_iterator max_i(
        max_element(states.begin(), states.end(), ToSeqCmpOp()));
    const gcomm::pc::Node& state(
        gcomm::pc::Proto::SMMap::get_value(max_i).get_node(
            gcomm::pc::Proto::SMMap::get_key(max_i)));
    return state.get_to_seq();
}

static void checksum(pc::Message& msg, gu::Datagram& dg)
{
    uint16_t crc16(gu::crc16(dg, 4));
    msg.checksum(crc16, true);
    pop_header(msg, dg);
    push_header(msg, dg);
}

static void test_checksum(pc::Message& msg, const gu::Datagram& dg,
                          size_t offset)
{
    uint16_t msg_crc16(msg.checksum());
    uint16_t crc16(gu::crc16(dg, offset + 4));
    if (crc16 != msg_crc16)
    {
        gu_throw_fatal << "Message checksum failed";
    }
}

//
//
//

void gcomm::pc::Proto::send_state()
{
    log_debug << self_id() << " sending state";

    StateMessage pcs(version_);

    NodeMap& im(pcs.get_node_map());

    for (NodeMap::iterator i = instances_.begin(); i != instances_.end(); ++i)
    {
        // Assume all nodes in the current view have reached current to_seq
        Node& local_state(NodeMap::get_value(i));
        if (current_view_.is_member(NodeMap::get_key(i)) == true)
        {
            local_state.set_to_seq(get_to_seq());
        }
        im.insert_unique(make_pair(NodeMap::get_key(i), local_state));
    }

    log_debug << self_id() << " local to seq " << get_to_seq();
    log_debug << self_id() << " sending state: " << pcs;

    Buffer buf;
    serialize(pcs, buf);
    Datagram dg(buf);

    if (send_down(dg, ProtoDownMeta()))
    {
        gu_throw_fatal << "pass down failed";
    }
}

void gcomm::pc::Proto::send_install()
{
    log_debug << self_id() << " send install";

    InstallMessage pci(version_);

    NodeMap& im(pci.get_node_map());

    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        if (current_view_.get_members().find(SMMap::get_key(i)) !=
            current_view_.get_members().end())
        {
            gu_trace(
                im.insert_unique(
                    make_pair(
                        SMMap::get_key(i),
                        SMMap::get_value(i).get_node((SMMap::get_key(i))))));
        }
    }

    log_debug << self_id() << " sending install: " << pci;

    Buffer buf;
    serialize(pci, buf);
    Datagram dg(buf);
    int ret = send_down(dg, ProtoDownMeta());
    if (ret != 0)
    {
        log_warn << self_id() << " sending install message failed: "
                 << strerror(ret);
    }
}


void gcomm::pc::Proto::deliver_view()
{
    View v(pc_view_.get_id());

    v.add_members(current_view_.get_members().begin(),
                  current_view_.get_members().end());

    for (NodeMap::const_iterator i = instances_.begin();
         i != instances_.end(); ++i)
    {
        if (current_view_.get_members().find(NodeMap::get_key(i)) ==
            current_view_.get_members().end())
        {
            v.add_partitioned(NodeMap::get_key(i), "");
        }
    }

    ProtoUpMeta um(UUID::nil(), ViewId(), &v);
    log_debug << self_id() << " delivering view " << v;
    send_up(Datagram(), um);
    set_stable_view(v);
}


void gcomm::pc::Proto::mark_non_prim()
{
    pc_view_ = ViewId(V_NON_PRIM, current_view_.get_id());
    for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
         ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        Node& inst(NodeMap::get_value(i));
        if (current_view_.get_members().find(uuid) !=
            current_view_.get_members().end())
        {
            inst.set_prim(false);
            pc_view_.add_member(uuid, "");
        }
    }

    set_prim(false);

}

void gcomm::pc::Proto::shift_to(const State s)
{
    // State graph
    static const bool allowed[S_MAX][S_MAX] = {

        // Closed
        { false, false,  false, false, false, true },
        // States exch
        { true,  false, true,  false, true,  true  },
        // Install
        { true,  false, false, true,  true,  true  },
        // Prim
        { true,  false, false, false, true,  true  },
        // Trans
        { true,  true,  false, false, false, true  },
        // Non-prim
        { true,  false,  false, false, true,  true  }
    };



    if (allowed[get_state()][s] == false)
    {
        gu_throw_fatal << "Forbidden state transtion: "
                       << to_string(get_state()) << " -> " << to_string(s);
    }

    switch (s)
    {
    case S_CLOSED:
        break;
    case S_STATES_EXCH:
        state_msgs_.clear();
        break;
    case S_INSTALL:
        break;
    case S_PRIM:
    {
        pc_view_ = ViewId(V_PRIM, current_view_.get_id());
        for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
             ++i)
        {
            const UUID& uuid(NodeMap::get_key(i));
            Node& inst(NodeMap::get_value(i));
            if (current_view_.get_members().find(uuid) !=
                current_view_.get_members().end())
            {
                inst.set_prim(true);
                inst.set_last_prim(ViewId(V_PRIM, current_view_.get_id()));
                inst.set_last_seq(0);
                inst.set_to_seq(get_to_seq());
                pc_view_.add_member(uuid, "");
            }
            else
            {
                inst.set_prim(false);
            }
        }
        last_sent_seq_ = 0;
        set_prim(true);
        break;
    }
    case S_TRANS:
        break;
    case S_NON_PRIM:
        mark_non_prim();
        break;
    default:
        ;
    }

    log_debug << self_id() << " shift_to: " << to_string(get_state())
              << " -> " <<  to_string(s)
              << " prim " << get_prim()
              << " last prim " << get_last_prim()
              << " to_seq " << get_to_seq();

    state_ = s;
}


void gcomm::pc::Proto::handle_first_trans(const View& view)
{
    gcomm_assert(get_state() == S_NON_PRIM);
    gcomm_assert(view.get_type() == V_TRANS);

    if (start_prim_ == true)
    {
        if (view.get_members().size() > 1 || view.is_empty())
        {
            gu_throw_fatal << "Corrupted view";
        }

        if (NodeList::get_key(view.get_members().begin()) != get_uuid())
        {
            gu_throw_fatal << "Bad first UUID: "
                           << NodeList::get_key(view.get_members().begin())
                           << ", expected: " << get_uuid();
        }

        set_last_prim(ViewId(V_PRIM, view.get_id()));
        set_prim(true);
    }
    current_view_ = view;
    shift_to(S_TRANS);
}


bool gcomm::pc::Proto::have_quorum(const View& view) const
{
    return (view.get_members().size()*2 + view.get_left().size() >
            pc_view_.get_members().size());
}


bool gcomm::pc::Proto::have_split_brain(const View& view) const
{
    return (view.get_members().size()*2 + view.get_left().size() ==
            pc_view_.get_members().size());
}


void gcomm::pc::Proto::handle_trans(const View& view)
{
    gcomm_assert(view.get_id().get_type() == V_TRANS);
    gcomm_assert(view.get_id().get_uuid() == current_view_.get_id().get_uuid() &&
                 view.get_id().get_seq()  == current_view_.get_id().get_seq());

    log_debug << self_id() << " \n\n current view " << current_view_
              << "\n\n next view " << view
              << "\n\n pc view " << pc_view_;

    if (!have_quorum(view))
    {
        if (closing_ == false && ignore_sb_ == true && have_split_brain(view))
        {
            // configured to ignore split brain
            log_warn << "Ignoring possible split-brain "
                     << "(allowed by configuration) from view:\n"
                     << current_view_ << "\nto view:\n" << view;
        }
        else if (closing_ == false && ignore_quorum_ == true)
        {
            // configured to ignore lack of quorum
            log_warn << "Ignoring lack of quorum "
                     << "(allowed by configuration) from view:\n"
                     << current_view_ << "\nto view:\n" << view;
        }
        else
        {
            current_view_ = view;
            // shift_to(S_NON_PRIM);
            mark_non_prim();
            deliver_view();
            shift_to(S_TRANS);
            return;
        }
    }
    else
    {
        log_debug << self_id() << " quorum ok";
    }
    current_view_ = view;
    shift_to(S_TRANS);
}


void gcomm::pc::Proto::handle_reg(const View& view)
{
    gcomm_assert(view.get_type() == V_REG);
    gcomm_assert(get_state() == S_TRANS);

    if (view.is_empty() == false &&
        view.get_id().get_seq() <= current_view_.get_id().get_seq())
    {
        gu_throw_fatal << "Non-increasing view ids: current view "
                       << current_view_.get_id()
                       << " new view "
                       << view.get_id();
    }

    current_view_ = view;
    views_.push_back(current_view_);

    if (current_view_.is_empty() == true)
    {
        shift_to(S_NON_PRIM);
        deliver_view();
        shift_to(S_CLOSED);
    }
    else
    {
        shift_to(S_STATES_EXCH);
        send_state();
    }
}


void gcomm::pc::Proto::handle_view(const View& view)
{

    // We accept only EVS TRANS and REG views
    if (view.get_type() != V_TRANS && view.get_type() != V_REG)
    {
        gu_throw_fatal << "Invalid view type";
    }

    // Make sure that self exists in view
    if (view.is_empty()            == false &&
        view.is_member(get_uuid()) == false)
    {
        gu_throw_fatal << "Self not found from non empty view: "
                       << view;
    }

    log_debug << self_id() << " " << view;

    if (view.get_type() == V_TRANS)
    {
        if (current_view_.get_type() == V_NONE)
        {
            handle_first_trans(view);
        }
        else
        {
            handle_trans(view);
        }
    }
    else
    {
        handle_reg(view);
    }
}




// Validate state message agains local state
void gcomm::pc::Proto::validate_state_msgs() const
{
    const int64_t max_to_seq(get_max_to_seq(state_msgs_));

    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        const UUID& msg_source_uuid(SMMap::get_key(i));
        const Node& msg_source_state(SMMap::get_value(i).get_node(msg_source_uuid));

        const NodeMap& msg_state_map(SMMap::get_value(i).get_node_map());
        for (NodeMap::const_iterator si = msg_state_map.begin();
             si != msg_state_map.end(); ++si)
        {
            const UUID& uuid(NodeMap::get_key(si));
            const Node& msg_state(NodeMap::get_value(si));
            const Node& local_state(NodeMap::get_value(instances_.find_checked(uuid)));
            if (get_prim()                  == true &&
                msg_source_state.get_prim() == true &&
                msg_state.get_prim()        == true)
            {
                if (current_view_.is_member(uuid) == true)
                {
                    // Msg source claims to come from prim view and this node
                    // is in prim. All message prim view states must be equal
                    // to local ones.
                    gcomm_assert(msg_state == local_state)
                        << self_id()
                        << " node " << uuid
                        << " prim state message and local states not consistent:"
                        << " msg node "   << msg_state
                        << " local state " << local_state;
                    gcomm_assert(msg_state.get_to_seq() == max_to_seq)
                        << self_id()
                        << " node " << uuid
                        << " to seq not consistent with local state:"
                        << " max to seq " << max_to_seq
                        << " msg state to seq " << msg_state.get_to_seq();
                }
            }
            else if (get_prim() == true)
            {
                log_debug << self_id()
                          << " node " << uuid
                          << " from " << msg_state.get_last_prim()
                          << " joining " << get_last_prim();
            }
            else if (msg_state.get_prim() == true)
            {
                // @todo: Cross check with other state messages coming from prim
                log_debug << self_id()
                          << " joining to " << msg_state.get_last_prim();
            }
        }
    }
}


// @note This method is currently for sanity checking only. RTR is not
// implemented yet.
bool gcomm::pc::Proto::requires_rtr() const
{
    bool ret = false;

    // Find maximum reported to_seq
    const int64_t max_to_seq(get_max_to_seq(state_msgs_));

    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        NodeMap::const_iterator ii(
            SMMap::get_value(i).get_node_map().find_checked(SMMap::get_key(i)));


        const Node& inst      = NodeMap::get_value(ii);
        const int64_t to_seq    = inst.get_to_seq();
        const ViewId  last_prim = inst.get_last_prim();

        if (to_seq                 != -1         &&
            to_seq                 != max_to_seq &&
            last_prim.get_type()   != V_NON_PRIM)
        {
            log_debug << self_id() << " RTR is needed: " << to_seq
                      << " / " << last_prim;
            ret = true;
        }
    }

    return ret;
}


void gcomm::pc::Proto::cleanup_instances()
{
    gcomm_assert(get_state() == S_PRIM);
    gcomm_assert(current_view_.get_type() == V_REG);

    NodeMap::iterator i, i_next;
    for (i = instances_.begin(); i != instances_.end(); i = i_next)
    {
        i_next = i, ++i_next;
        const UUID& uuid(NodeMap::get_key(i));
        if (current_view_.is_member(uuid) == false)
        {
            log_debug << self_id()
                      << " cleaning up instance " << uuid;
            instances_.erase(i);
        }
    }
}


bool gcomm::pc::Proto::is_prim() const
{
    bool prim(false);
    ViewId last_prim(V_NON_PRIM);
    int64_t to_seq(-1);

    // Check if any of instances claims to come from prim view
    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        const Node& state(SMMap::get_value(i).get_node(SMMap::get_key(i)));

        if (state.get_prim() == true)
        {
            prim      = true;
            last_prim = state.get_last_prim();
            to_seq    = state.get_to_seq();
            break;
        }
    }

    // Verify that all members are either coming from the same prim
    // view or from non-prim
    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        const Node& state(SMMap::get_value(i).get_node(SMMap::get_key(i)));

        if (state.get_prim() == true)
        {
            if (state.get_last_prim() != last_prim)
            {
                gu_throw_fatal
                    << self_id()
                    << " last prims not consistent";
            }

            if (state.get_to_seq() != to_seq)
            {
                gu_throw_fatal
                    << self_id()
                    << " TO seqs not consistent";
            }
        }
        else
        {
            log_debug << "Non-prim " << SMMap::get_key(i) <<" from "
                      << state.get_last_prim() << " joining prim";
        }
    }

    // No members coming from prim view, check if last known prim
    // view can be recovered (majority of members from last prim alive)
    if (prim == false)
    {
        gcomm_assert(last_prim == ViewId(V_NON_PRIM))
            << last_prim << " != " << ViewId(V_NON_PRIM);

        MultiMap<ViewId, UUID> last_prim_uuids;

        for (SMMap::const_iterator i = state_msgs_.begin();
             i != state_msgs_.end();
             ++i)
        {
            for (NodeMap::const_iterator
                     j = SMMap::get_value(i).get_node_map().begin();
                 j != SMMap::get_value(i).get_node_map().end(); ++j)
            {
                const UUID& uuid(NodeMap::get_key(j));
                const Node& inst(NodeMap::get_value(j));

                if (inst.get_last_prim() != ViewId(V_NON_PRIM) &&
                    find<MultiMap<ViewId, UUID>::iterator,
                    pair<const ViewId, UUID> >(last_prim_uuids.begin(),
                                               last_prim_uuids.end(),
                                               make_pair(inst.get_last_prim(), uuid)) ==
                    last_prim_uuids.end())
                {
                    last_prim_uuids.insert(make_pair(inst.get_last_prim(), uuid));
                }
            }
        }

        if (last_prim_uuids.empty() == true)
        {
            log_warn << "no nodes coming from prim view, prim not possible";
            return false;
        }

        const ViewId greatest_view_id(last_prim_uuids.rbegin()->first);
        set<UUID> greatest_view;
        pair<MultiMap<ViewId, UUID>::const_iterator,
            MultiMap<ViewId, UUID>::const_iterator> gvi =
            last_prim_uuids.equal_range(greatest_view_id);
        for (MultiMap<ViewId, UUID>::const_iterator i = gvi.first;
             i != gvi.second; ++i)
        {
            pair<set<UUID>::iterator, bool> iret = greatest_view.insert(
                MultiMap<ViewId, UUID>::get_value(i));
            gcomm_assert(iret.second == true);
        }
        log_debug << self_id()
                  << " greatest view id " << greatest_view_id;
        set<UUID> present;
        for (NodeList::const_iterator i = current_view_.get_members().begin();
             i != current_view_.get_members().end(); ++i)
        {
            present.insert(NodeList::get_key(i));
        }
        set<UUID> intersection;
        set_intersection(greatest_view.begin(), greatest_view.end(),
                         present.begin(), present.end(),
                         inserter(intersection, intersection.begin()));
        log_debug << self_id()
                  << " intersection size " << intersection.size()
                  << " greatest view size " << greatest_view.size();
        if (intersection.size() == greatest_view.size())
        {
            prim = true;
        }
    }

    return prim;
}


void gcomm::pc::Proto::handle_state(const Message& msg, const UUID& source)
{
    gcomm_assert(msg.get_type() == Message::T_STATE);
    gcomm_assert(get_state() == S_STATES_EXCH);
    gcomm_assert(state_msgs_.size() < current_view_.get_members().size());

    log_debug << self_id() << " handle state from " << source << " " << msg;

    // Early check for possibly conflicting primary components. The one
    // with greater view id may continue (as it probably has been around
    // for longer timer). However, this should be configurable policy.
    if (get_prim() == true)
    {
        const Node& si(NodeMap::get_value(msg.get_node_map().find(source)));
        if (si.get_prim() == true && si.get_last_prim() != get_last_prim())
        {
            log_warn << self_id() << " conflicting prims: my prim: "
                     << get_last_prim()
                     << " other prim: "
                     << si.get_last_prim();

            if ((npvo_ == true  && get_last_prim() < si.get_last_prim()) ||
                (npvo_ == false && get_last_prim() > si.get_last_prim()))
            {
                log_warn << self_id() << " discarding other prim view: "
                         << (npvo_ == true ? "newer" : "older" )
                         << " overrides";
                return;
            }
            else
            {
                gu_throw_fatal << self_id()
                               << " aborting due to conflicting prims: "
                               << (npvo_ == true ? "newer" : "older" )
                               << " overrides";
            }
        }
    }

    state_msgs_.insert_unique(make_pair(source, msg));

    if (state_msgs_.size() == current_view_.get_members().size())
    {
        // Insert states from previously unseen nodes into local state map
        for (SMMap::const_iterator i = state_msgs_.begin();
             i != state_msgs_.end(); ++i)
        {
            const NodeMap& sm_im(SMMap::get_value(i).get_node_map());
            for (NodeMap::const_iterator j = sm_im.begin(); j != sm_im.end();
                 ++j)
            {
                const UUID& sm_uuid(NodeMap::get_key(j));
                if (instances_.find(sm_uuid) == instances_.end())
                {
                    const Node& sm_state(NodeMap::get_value(j));
                    instances_.insert_unique(make_pair(sm_uuid, sm_state));
                }
            }
        }

        // Validate that all state messages are consistent before proceeding
        gu_trace(validate_state_msgs());

        if (is_prim() == true)
        {
            // @note Requires RTR does not actually have effect, but let it
            // be for debugging purposes until a while
            (void)requires_rtr();
            shift_to(S_INSTALL);

            if (current_view_.get_members().find(get_uuid()) ==
                current_view_.get_members().begin())
            {
                send_install();
            }
        }
        else
        {
            // #571 Deliver NON-PRIM views in all cases.
            // const bool was_prim(get_prim());
            shift_to(S_NON_PRIM);
            // if (was_prim == true)
            // {
            deliver_view();
            // }
        }
    }
}


void gcomm::pc::Proto::handle_install(const Message& msg, const UUID& source)
{
    gcomm_assert(msg.get_type() == Message::T_INSTALL);
    gcomm_assert(get_state()    == S_INSTALL);

    log_debug << self_id()
              << " handle install from " << source << " " << msg;

    // Validate own state

    NodeMap::const_iterator mi(msg.get_node_map().find_checked(get_uuid()));

    const Node& m_state(NodeMap::get_value(mi));

    if (m_state != NodeMap::get_value(self_i_))
    {
        gu_throw_fatal << self_id()
                       << "Install message self state does not match, "
                       << "message state: " << m_state
                       << ", local state: "
                       << NodeMap::get_value(self_i_);
    }

    // Set TO seqno according to install message
    int64_t to_seq(-1);

    for (mi = msg.get_node_map().begin(); mi != msg.get_node_map().end(); ++mi)
    {
        const Node& m_state = NodeMap::get_value(mi);

        if (m_state.get_prim() == true && to_seq != -1)
        {
            if (m_state.get_to_seq() != to_seq)
            {
                gu_throw_fatal << "Install message TO seqno inconsistent";
            }
        }

        if (m_state.get_prim() == true)
        {
            to_seq = max(to_seq, m_state.get_to_seq());
        }
    }

    log_debug << self_id() << " setting TO seq to " << to_seq;

    set_to_seq(to_seq);

    shift_to(S_PRIM);
    deliver_view();
    cleanup_instances();
}


void gcomm::pc::Proto::handle_user(const Message& msg, const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    int64_t to_seq(-1);

    if (get_prim() == true)
    {
        if (um.get_order() == O_SAFE)
        {
            set_to_seq(get_to_seq() + 1);
            to_seq = get_to_seq();
        }
    }
    else if (current_view_.get_members().find(um.get_source()) ==
             current_view_.get_members().end())
    {
        gcomm_assert(current_view_.get_type() == V_TRANS);
        // log_debug << self_id()
        //        << " dropping message from out of view source in non-prim";
        return;
    }


    if (um.get_order() == O_SAFE)
    {
        Node& state(NodeMap::get_value(instances_.find_checked(um.get_source())));
        if (state.get_last_seq() + 1 != msg.get_seq())
        {
            gu_throw_fatal << "gap in message sequence: source="
                           << um.get_source()
                           << " expected_seq="
                           << state.get_last_seq() + 1
                           << " seq="
                           << msg.get_seq();
        }
        state.set_last_seq(msg.get_seq());
    }

    Datagram up_dg(dg, dg.get_offset() + msg.serial_size());
    gu_trace(send_up(up_dg,
                     ProtoUpMeta(um.get_source(),
                                 pc_view_.get_id(),
                                 0,
                                 um.get_user_type(),
                                 um.get_order(),
                                 to_seq)));
}


void gcomm::pc::Proto::handle_msg(const Message&   msg,
                         const Datagram&    rb,
                         const ProtoUpMeta& um)
{
    enum Verdict
    {
        ACCEPT,
        DROP,
        FAIL
    };

    static const Verdict verdicts[S_MAX][Message::T_MAX] = {
        // Msg types
        // NONE,   STATE,   INSTALL,  USER
        {  FAIL,   FAIL,    FAIL,     FAIL    },  // Closed

        {  FAIL,   ACCEPT,  FAIL,     FAIL    },  // States exch

        {  FAIL,   FAIL,    ACCEPT,   FAIL    },  // INSTALL

        {  FAIL,   FAIL,    FAIL,     ACCEPT  },  // PRIM

        {  FAIL,   DROP,    DROP,     ACCEPT  },  // TRANS

        {  FAIL,   ACCEPT,  FAIL,     ACCEPT  }   // NON-PRIM
    };

    Message::Type msg_type(msg.get_type());
    Verdict       verdict (verdicts[get_state()][msg.get_type()]);

    if (verdict == FAIL)
    {
        gu_throw_fatal << "Invalid input, message " << msg.to_string()
                       << " in state " << to_string(get_state());
    }
    else if (verdict == DROP)
    {
        log_warn << "Dropping input, message " << msg.to_string()
                 << " in state " << to_string(get_state());
        return;
    }

    switch (msg_type)
    {
    case Message::T_STATE:
        gu_trace(handle_state(msg, um.get_source()));
        break;
    case Message::T_INSTALL:
        gu_trace(handle_install(msg, um.get_source()));
        break;
    case Message::T_USER:
        gu_trace(handle_user(msg, rb, um));
        break;
    default:
        gu_throw_fatal << "Invalid message";
    }
}


void gcomm::pc::Proto::handle_up(const void* cid,
                                 const Datagram& rb,
                                 const ProtoUpMeta& um)
{
    if (um.has_view() == true)
    {
        handle_view(um.get_view());
    }
    else
    {
        Message msg;
        const byte_t* b(get_begin(rb));
        const size_t available(get_available(rb));
        try
        {
            (void)msg.unserialize(b, available, 0);
        }
        catch (Exception& e)
        {
            switch (e.get_errno())
            {
            case EPROTONOSUPPORT:
                if (get_prim() == false)
                {
                    gu_throw_fatal << e.what() << " terminating";
                }
                else
                {
                    log_warn << "unknown/unsupported protocol version: "
                             << msg.get_version()
                             << " dropping message";
                    return;
                }

                break;

            default:
                GU_TRACE(e);
                throw;
            }
        }

        if (checksum_ == true && msg.flags() & Message::F_CRC16)
        {
            test_checksum(msg, rb, rb.get_offset());
        }

        handle_msg(msg, rb, um);
    }
}


int gcomm::pc::Proto::handle_down(Datagram& dg, const ProtoDownMeta& dm)
{
    if (gu_unlikely(get_state() != S_PRIM))
    {
        return EAGAIN;
    }
    if (gu_unlikely(dg.get_len() > mtu()))
    {
        return EMSGSIZE;
    }

    uint32_t    seq(dm.get_order() == O_SAFE ? last_sent_seq_ + 1 : last_sent_seq_);
    UserMessage um(version_, seq);

    push_header(um, dg);
    if (checksum_ == true)
    {
        checksum(um, dg);
    }

    int ret = send_down(dg, dm);

    if (ret == 0)
    {
        last_sent_seq_ = seq;
    }
    else if (ret != EAGAIN)
    {
        log_warn << "Proto::handle_down: " << strerror(ret);
    }

    pop_header(um, dg);

    return ret;
}


bool gcomm::pc::Proto::set_param(const std::string& key,
                                 const std::string& value)
{
    if (key == gcomm::Conf::PcIgnoreSb)
    {
        ignore_sb_ = gu::from_string<bool>(value);
        return true;
    }

    if (key == gcomm::Conf::PcIgnoreQuorum)
    {
        ignore_quorum_ = gu::from_string<bool>(value);
        return true;
    }

    return false;
}
