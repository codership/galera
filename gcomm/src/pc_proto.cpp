/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "pc_proto.hpp"
#include "pc_message.hpp"

#include "gcomm/util.hpp"

#include "gu_logger.hpp"
#include "gu_macros.h"
#include <set>

using std::rel_ops::operator!=;
using std::rel_ops::operator>;
//
// Helpers
//

class SelectPrimOp
{
public:
    SelectPrimOp(gcomm::pc::Proto::SMMap& states) : states_(states) { }
    void operator()(const gcomm::pc::Proto::SMMap::value_type& vt) const
    {
        const gcomm::UUID& uuid(gcomm::pc::Proto::SMMap::key(vt));
        const gcomm::pc::Message& msg(gcomm::pc::Proto::SMMap::value(vt));
        const gcomm::pc::NodeMap& nm(msg.node_map());
        gcomm::pc::NodeMap::const_iterator nm_i(nm.find(uuid));
        if (nm_i == nm.end())
        {
            gu_throw_error(EPROTO) << "protocol error, self not found from "
                                   << uuid << " state msg node list";
        }
        if (gcomm::pc::NodeMap::value(nm_i).prim() == true)
        {
            states_.insert(vt);
        }
    }
private:
    gcomm::pc::Proto::SMMap& states_;
};

class ToSeqCmpOp
{
public:
    bool operator()(const gcomm::pc::Proto::SMMap::value_type& a,
                    const gcomm::pc::Proto::SMMap::value_type& b) const
    {
        const gcomm::pc::Node& astate(
            gcomm::pc::NodeMap::value(
                gcomm::pc::Proto::SMMap::value(a).node_map()
                .find_checked(gcomm::pc::Proto::SMMap::key(a))));

        const gcomm::pc::Node& bstate(
            gcomm::pc::NodeMap::value(
                gcomm::pc::Proto::SMMap::value(b).node_map()
                .find_checked(gcomm::pc::Proto::SMMap::key(b))));

        return (astate.to_seq() < bstate.to_seq());
    }
};

// Return max to seq found from states, -1 if states is empty
static int64_t get_max_to_seq(const gcomm::pc::Proto::SMMap& states)
{
    if (states.empty() == true) return -1;

    gcomm::pc::Proto::SMMap::const_iterator max_i(
        max_element(states.begin(), states.end(), ToSeqCmpOp()));
    const gcomm::pc::Node& state(
        gcomm::pc::Proto::SMMap::value(max_i).node(
            gcomm::pc::Proto::SMMap::key(max_i)));
    return state.to_seq();
}

static void checksum(gcomm::pc::Message& msg, gcomm::Datagram& dg)
{
    uint16_t crc16(gcomm::crc16(dg, 4));
    msg.checksum(crc16, true);
    gcomm::pop_header(msg, dg);
    gcomm::push_header(msg, dg);
}

static void test_checksum(gcomm::pc::Message& msg, const gcomm::Datagram& dg,
                          size_t offset)
{
    uint16_t msg_crc16(msg.checksum());
    uint16_t crc16(gcomm::crc16(dg, offset + 4));
    if (crc16 != msg_crc16)
    {
        gu_throw_fatal << "Message checksum failed";
    }
}

std::ostream& gcomm::pc::operator<<(std::ostream& os, const gcomm::pc::Proto& p)
{
    os << "pc::Proto{";
    os << "uuid=" << p.my_uuid_ << ",";
    os << "start_prim=" << p.start_prim_ << ",";
    os << "npvo=" << p.npvo_ << ",";
    os << "ignore_sb=" << p.ignore_sb_ << ",";
    os << "ignore_quorum=" << p.ignore_quorum_ << ",";
    os << "state=" << p.state_ << ",";
    os << "last_sent_seq=" << p.last_sent_seq_ << ",";
    os << "checksum=" << p.checksum_ << ",";
    os << "instances=\n" << p.instances_ << ",";
    os << "state_msgs=\n" << p.state_msgs_ << ",";
    os << "current_view=" << p.current_view_ << ",";
    os << "pc_view=" << p.pc_view_ << ",";
    // os << "views=" << p.views_ << ",";
    os << "mtu=" << p.mtu_ << "}";
    return os;
}


//
//
//

void gcomm::pc::Proto::send_state()
{
    log_debug << self_id() << " sending state";

    StateMessage pcs(version_);

    NodeMap& im(pcs.node_map());

    for (NodeMap::iterator i = instances_.begin(); i != instances_.end(); ++i)
    {
        // Assume all nodes in the current view have reached current to_seq
        Node& local_state(NodeMap::value(i));
        if (current_view_.is_member(NodeMap::key(i)) == true)
        {
            local_state.set_to_seq(to_seq());
        }
        im.insert_unique(std::make_pair(NodeMap::key(i), local_state));
    }

    log_debug << self_id() << " local to seq " << to_seq();
    log_debug << self_id() << " sending state: " << pcs;

    gu::Buffer buf;
    serialize(pcs, buf);
    Datagram dg(buf);

    if (send_down(dg, ProtoDownMeta()))
    {
        gu_throw_fatal << "pass down failed";
    }
}

void gcomm::pc::Proto::send_install(bool bootstrap)
{
    log_debug << self_id() << " send install";

    InstallMessage pci(version_);

    NodeMap& im(pci.node_map());

    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        if (current_view_.members().find(SMMap::key(i)) !=
            current_view_.members().end())
        {
            gu_trace(
                im.insert_unique(
                    std::make_pair(
                        SMMap::key(i),
                        SMMap::value(i).node((SMMap::key(i))))));
        }
    }

    if (bootstrap == true)
    {
        pci.flags(pci.flags() | InstallMessage::F_BOOTSTRAP);
        log_debug << self_id() << " sending PC bootstrap message " << pci;
    }
    else
    {
        log_debug << self_id() << " sending install: " << pci;
    }

    gu::Buffer buf;
    serialize(pci, buf);
    Datagram dg(buf);
    int ret = send_down(dg, ProtoDownMeta());
    if (ret != 0)
    {
        log_warn << self_id() << " sending install message failed: "
                 << strerror(ret);
    }
}


void gcomm::pc::Proto::deliver_view(bool bootstrap)
{
    View v(pc_view_.id(), bootstrap);

    v.add_members(current_view_.members().begin(),
                  current_view_.members().end());

    for (NodeMap::const_iterator i = instances_.begin();
         i != instances_.end(); ++i)
    {
        if (current_view_.members().find(NodeMap::key(i)) ==
            current_view_.members().end())
        {
            v.add_partitioned(NodeMap::key(i), "");
        }
    }

    ProtoUpMeta um(UUID::nil(), ViewId(), &v);
    log_info << v;
    send_up(Datagram(), um);
    set_stable_view(v);
}


void gcomm::pc::Proto::mark_non_prim()
{
    pc_view_ = ViewId(V_NON_PRIM, current_view_.id());
    for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
         ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        Node& inst(NodeMap::value(i));
        if (current_view_.members().find(uuid) !=
            current_view_.members().end())
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
        { true,  false,  false, true, true,  true  }
    };



    if (allowed[state()][s] == false)
    {
        gu_throw_fatal << "Forbidden state transtion: "
                       << to_string(state()) << " -> " << to_string(s);
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
        pc_view_ = ViewId(V_PRIM, current_view_.id());
        for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
             ++i)
        {
            const UUID& uuid(NodeMap::key(i));
            Node& inst(NodeMap::value(i));
            if (current_view_.members().find(uuid) !=
                current_view_.members().end())
            {
                inst.set_prim(true);
                inst.set_last_prim(ViewId(V_PRIM, current_view_.id()));
                inst.set_last_seq(0);
                inst.set_to_seq(to_seq());
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

    log_debug << self_id() << " shift_to: " << to_string(state())
              << " -> " <<  to_string(s)
              << " prim " << prim()
              << " last prim " << last_prim()
              << " to_seq " << to_seq();

    state_ = s;
}


void gcomm::pc::Proto::handle_first_trans(const View& view)
{
    gcomm_assert(state() == S_NON_PRIM);
    gcomm_assert(view.type() == V_TRANS);

    if (start_prim_ == true)
    {
        if (view.members().size() > 1 || view.is_empty())
        {
            gu_throw_fatal << "Corrupted view";
        }

        if (NodeList::key(view.members().begin()) != uuid())
        {
            gu_throw_fatal << "Bad first UUID: "
                           << NodeList::key(view.members().begin())
                           << ", expected: " << uuid();
        }

        set_last_prim(ViewId(V_PRIM, view.id()));
        set_prim(true);
    }
    current_view_ = view;
    shift_to(S_TRANS);
}

// Compute weighted sum of members in node list. If member cannot be found
// from node_map its weight is assumed to be zero.
static size_t weighted_sum(const gcomm::NodeList& node_list,
                           const gcomm::pc::NodeMap& node_map)
{
    size_t sum(0);
    for (gcomm::NodeList::const_iterator i(node_list.begin());
         i != node_list.end(); ++i)
    {
        int weight(0);
        gcomm::pc::NodeMap::const_iterator node_i(
            node_map.find(gcomm::NodeList::key(i)));
        if (node_i != node_map.end())
        {
            const gcomm::pc::Node& node(gcomm::pc::NodeMap::value(node_i));
            gcomm_assert(node.weight() >= 0 &&
                         node.weight() <= 0xff);
            weight = node.weight();
        }
        else
        {
            weight = 0;
        }
        sum += weight;
    }
    return sum;
}

// Check if all members in node_list have weight associated. This is needed
// to fall back to backwards compatibility mode during upgrade (all weights are
// assumed to be one). See have_quorum() and have_split_brain() below.
static bool have_weights(const gcomm::NodeList& node_list,
                         const gcomm::pc::NodeMap& node_map)
{
    for (gcomm::NodeList::const_iterator i(node_list.begin());
         i != node_list.end(); ++i)
    {
        gcomm::pc::NodeMap::const_iterator node_i(
            node_map.find(gcomm::NodeList::key(i)));
        if (node_i != node_map.end())
        {
            const gcomm::pc::Node& node(gcomm::pc::NodeMap::value(node_i));
            if (node.weight() == -1)
            {
                return false;
            }
        }
    }
    return true;
}


bool gcomm::pc::Proto::have_quorum(const View& view) const
{
    if (have_weights(view.members(), instances_) &&
        have_weights(view.left(), instances_)    &&
        have_weights(pc_view_.members(), instances_))
    {
        return (weighted_sum(view.members(), instances_) * 2
                + weighted_sum(view.left(), instances_) >
                weighted_sum(pc_view_.members(), instances_));
    }
    else
    {
        return (view.members().size()*2 + view.left().size() >
                pc_view_.members().size());
    }
}


bool gcomm::pc::Proto::have_split_brain(const View& view) const
{
    if (have_weights(view.members(), instances_)  &&
        have_weights(view.left(), instances_)     &&
        have_weights(pc_view_.members(), instances_))
    {
        return (weighted_sum(view.members(), instances_) * 2
                + weighted_sum(view.left(), instances_) ==
                weighted_sum(pc_view_.members(), instances_));
    }
    else
    {
        return (view.members().size()*2 + view.left().size() ==
                pc_view_.members().size());
    }
}


void gcomm::pc::Proto::handle_trans(const View& view)
{
    gcomm_assert(view.id().type() == V_TRANS);
    gcomm_assert(view.id().uuid() == current_view_.id().uuid() &&
                 view.id().seq()  == current_view_.id().seq());

    log_debug << self_id() << " \n\n current view " << current_view_
              << "\n\n next view " << view
              << "\n\n pc view " << pc_view_;

    if (have_quorum(view) == false)
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
    gcomm_assert(view.type() == V_REG);
    gcomm_assert(state() == S_TRANS);

    if (view.is_empty() == false &&
        view.id().seq() <= current_view_.id().seq())
    {
        gu_throw_fatal << "Non-increasing view ids: current view "
                       << current_view_.id()
                       << " new view "
                       << view.id();
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
    if (view.type() != V_TRANS && view.type() != V_REG)
    {
        gu_throw_fatal << "Invalid view type";
    }

    // Make sure that self exists in view
    if (view.is_empty()            == false &&
        view.is_member(uuid()) == false)
    {
        gu_throw_fatal << "Self not found from non empty view: "
                       << view;
    }

    log_debug << self_id() << " " << view;

    if (view.type() == V_TRANS)
    {
        if (current_view_.type() == V_NONE)
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
    // #622, #638 Compute max TO seq among states from prim
    SMMap prim_state_msgs;
    std::for_each(state_msgs_.begin(), state_msgs_.end(),
                  SelectPrimOp(prim_state_msgs));
    const int64_t max_to_seq(get_max_to_seq(prim_state_msgs));

    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        const UUID& msg_source_uuid(SMMap::key(i));
        const Node& msg_source_state(SMMap::value(i).node(msg_source_uuid));

        const NodeMap& msg_state_map(SMMap::value(i).node_map());
        for (NodeMap::const_iterator si = msg_state_map.begin();
             si != msg_state_map.end(); ++si)
        {
            const UUID& uuid(NodeMap::key(si));
            const Node& msg_state(NodeMap::value(si));
            const Node& local_state(NodeMap::value(instances_.find_checked(uuid)));
            if (prim()                  == true &&
                msg_source_state.prim() == true &&
                msg_state.prim()        == true)
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
                    gcomm_assert(msg_state.to_seq() == max_to_seq)
                        << self_id()
                        << " node " << uuid
                        << " to seq not consistent with local state:"
                        << " max to seq " << max_to_seq
                        << " msg state to seq " << msg_state.to_seq();
                }
            }
            else if (prim() == true)
            {
                log_debug << self_id()
                          << " node " << uuid
                          << " from " << msg_state.last_prim()
                          << " joining " << last_prim();
            }
            else if (msg_state.prim() == true)
            {
                // @todo: Cross check with other state messages coming from prim
                log_debug << self_id()
                          << " joining to " << msg_state.last_prim();
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
            SMMap::value(i).node_map().find_checked(SMMap::key(i)));


        const Node& inst      = NodeMap::value(ii);
        const int64_t to_seq    = inst.to_seq();
        const ViewId  last_prim = inst.last_prim();

        if (to_seq                 != -1         &&
            to_seq                 != max_to_seq &&
            last_prim.type()   != V_NON_PRIM)
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
    gcomm_assert(state() == S_PRIM);
    gcomm_assert(current_view_.type() == V_REG);

    NodeMap::iterator i, i_next;
    for (i = instances_.begin(); i != instances_.end(); i = i_next)
    {
        i_next = i, ++i_next;
        const UUID& uuid(NodeMap::key(i));
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
        const Node& state(SMMap::value(i).node(SMMap::key(i)));

        if (state.prim() == true)
        {
            prim      = true;
            last_prim = state.last_prim();
            to_seq    = state.to_seq();
            break;
        }
    }

    // Verify that all members are either coming from the same prim
    // view or from non-prim
    for (SMMap::const_iterator i = state_msgs_.begin(); i != state_msgs_.end();
         ++i)
    {
        const Node& state(SMMap::value(i).node(SMMap::key(i)));

        if (state.prim() == true)
        {
            if (state.last_prim() != last_prim)
            {
                gu_throw_fatal
                    << self_id()
                    << " last prims not consistent";
            }

            if (state.to_seq() != to_seq)
            {
                gu_throw_fatal
                    << self_id()
                    << " TO seqs not consistent";
            }
        }
        else
        {
            log_debug << "Non-prim " << SMMap::key(i) <<" from "
                      << state.last_prim() << " joining prim";
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
                     j = SMMap::value(i).node_map().begin();
                 j != SMMap::value(i).node_map().end(); ++j)
            {
                const UUID& uuid(NodeMap::key(j));
                const Node& inst(NodeMap::value(j));

                if (inst.last_prim() != ViewId(V_NON_PRIM) &&
                    std::find<MultiMap<ViewId, UUID>::iterator,
                              std::pair<const ViewId, UUID> >(
                                  last_prim_uuids.begin(),
                                  last_prim_uuids.end(),
                                  std::make_pair(inst.last_prim(), uuid)) ==
                    last_prim_uuids.end())
                {
                    last_prim_uuids.insert(std::make_pair(inst.last_prim(), uuid));
                }
            }
        }

        if (last_prim_uuids.empty() == true)
        {
            log_warn << "no nodes coming from prim view, prim not possible";
            return false;
        }

        const ViewId greatest_view_id(last_prim_uuids.rbegin()->first);
        std::set<UUID> greatest_view;
        std::pair<MultiMap<ViewId, UUID>::const_iterator,
                  MultiMap<ViewId, UUID>::const_iterator> gvi =
            last_prim_uuids.equal_range(greatest_view_id);
        for (MultiMap<ViewId, UUID>::const_iterator i = gvi.first;
             i != gvi.second; ++i)
        {
            std::pair<std::set<UUID>::iterator, bool>
                iret = greatest_view.insert(
                    MultiMap<ViewId, UUID>::value(i));
            gcomm_assert(iret.second == true);
        }
        log_debug << self_id()
                  << " greatest view id " << greatest_view_id;
        std::set<UUID> present;
        for (NodeList::const_iterator i = current_view_.members().begin();
             i != current_view_.members().end(); ++i)
        {
            present.insert(NodeList::key(i));
        }
        std::set<UUID> intersection;
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
    gcomm_assert(msg.type() == Message::T_STATE);
    gcomm_assert(state() == S_STATES_EXCH);
    gcomm_assert(state_msgs_.size() < current_view_.members().size());

    log_debug << self_id() << " handle state from " << source << " " << msg;

    // Early check for possibly conflicting primary components. The one
    // with greater view id may continue (as it probably has been around
    // for longer timer). However, this should be configurable policy.
    if (prim() == true)
    {
        const Node& si(NodeMap::value(msg.node_map().find(source)));
        if (si.prim() == true && si.last_prim() != last_prim())
        {
            log_warn << self_id() << " conflicting prims: my prim: "
                     << last_prim()
                     << " other prim: "
                     << si.last_prim();

            if ((npvo_ == true  && last_prim() < si.last_prim()) ||
                (npvo_ == false && last_prim() > si.last_prim()))
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

    state_msgs_.insert_unique(std::make_pair(source, msg));

    if (state_msgs_.size() == current_view_.members().size())
    {
        // Insert states from previously unseen nodes into local state map
        for (SMMap::const_iterator i = state_msgs_.begin();
             i != state_msgs_.end(); ++i)
        {
            const NodeMap& sm_im(SMMap::value(i).node_map());
            for (NodeMap::const_iterator j = sm_im.begin(); j != sm_im.end();
                 ++j)
            {
                const UUID& sm_uuid(NodeMap::key(j));
                if (instances_.find(sm_uuid) == instances_.end())
                {
                    const Node& sm_state(NodeMap::value(j));
                    instances_.insert_unique(std::make_pair(sm_uuid, sm_state));
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

            if (current_view_.members().find(uuid()) ==
                current_view_.members().begin())
            {
                send_install(false);
            }
        }
        else
        {
            // #571 Deliver NON-PRIM views in all cases.
            shift_to(S_NON_PRIM);
            deliver_view();
        }
    }
}


void gcomm::pc::Proto::handle_install(const Message& msg, const UUID& source)
{
    gcomm_assert(msg.type() == Message::T_INSTALL);
    gcomm_assert(state() == S_INSTALL || state() == S_NON_PRIM);

    if ((msg.flags() & Message::F_BOOTSTRAP) == 0)
    {
        log_debug << self_id()
                  << " handle install from " << source << " " << msg;
    }
    else
    {
        log_debug << self_id()
                  << " handle bootstrap install from " << source << " " << msg;
        if (state() == S_INSTALL)
        {
            log_info << "ignoring bootstrap install in "
                     << to_string(state()) << " state";
            return;
        }
    }
    // Validate own state

    NodeMap::const_iterator mi(msg.node_map().find_checked(uuid()));

    const Node& m_state(NodeMap::value(mi));

    if (m_state != NodeMap::value(self_i_))
    {
        gu_throw_fatal << self_id()
                       << "Install message self state does not match, "
                       << "message state: " << m_state
                       << ", local state: "
                       << NodeMap::value(self_i_);
    }

    // Set TO seqno according to install message
    int64_t to_seq(-1);
    bool prim_found(false);
    for (mi = msg.node_map().begin(); mi != msg.node_map().end(); ++mi)
    {
        const Node& m_state = NodeMap::value(mi);
        // check that all TO seqs coming from prim are same
        if (m_state.prim() == true && to_seq != -1)
        {
            if (m_state.to_seq() != to_seq)
            {
                gu_throw_fatal << "Install message TO seqnos inconsistent";
            }
        }

        if (m_state.prim() == true)
        {
            prim_found = true;
            to_seq = std::max(to_seq, m_state.to_seq());
        }
    }

    if (prim_found == false)
    {
        // #277
        // prim comp was restored from non-prims, find out max known TO seq
        for (mi = msg.node_map().begin(); mi != msg.node_map().end();
             ++mi)
        {
            const Node& m_state = NodeMap::value(mi);
            to_seq = std::max(to_seq, m_state.to_seq());
        }
        log_debug << "assigning TO seq to "
                  << to_seq << " after restoring prim";
    }


    log_debug << self_id() << " setting TO seq to " << to_seq;

    set_to_seq(to_seq);

    shift_to(S_PRIM);
    deliver_view(msg.flags() & Message::F_BOOTSTRAP);
    cleanup_instances();
}


void gcomm::pc::Proto::handle_user(const Message& msg, const Datagram& dg,
                                   const ProtoUpMeta& um)
{
    int64_t curr_to_seq(-1);

    if (prim() == true)
    {
        if (um.order() == O_SAFE)
        {
            set_to_seq(to_seq() + 1);
            curr_to_seq = to_seq();
        }
    }
    else if (current_view_.members().find(um.source()) ==
             current_view_.members().end())
    {
        gcomm_assert(current_view_.type() == V_TRANS);
        // log_debug << self_id()
        //        << " dropping message from out of view source in non-prim";
        return;
    }


    if (um.order() == O_SAFE)
    {
        Node& state(NodeMap::value(instances_.find_checked(um.source())));
        if (state.last_seq() + 1 != msg.seq())
        {
            gu_throw_fatal << "gap in message sequence: source="
                           << um.source()
                           << " expected_seq="
                           << state.last_seq() + 1
                           << " seq="
                           << msg.seq();
        }
        state.set_last_seq(msg.seq());
    }

    Datagram up_dg(dg, dg.offset() + msg.serial_size());
    gu_trace(send_up(up_dg,
                     ProtoUpMeta(um.source(),
                                 pc_view_.id(),
                                 0,
                                 um.user_type(),
                                 um.order(),
                                 curr_to_seq)));
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

        {  FAIL,   FAIL,    DROP,     ACCEPT  },  // PRIM

        {  FAIL,   DROP,    DROP,     ACCEPT  },  // TRANS

        {  FAIL,   ACCEPT,  ACCEPT,   ACCEPT  }   // NON-PRIM
    };

    Message::Type msg_type(msg.type());
    Verdict       verdict (verdicts[state()][msg.type()]);

    if (verdict == FAIL)
    {
        gu_throw_fatal << "Invalid input, message " << msg.to_string()
                       << " in state " << to_string(state());
    }
    else if (verdict == DROP)
    {
        log_warn << "Dropping input, message " << msg.to_string()
                 << " in state " << to_string(state());
        return;
    }

    switch (msg_type)
    {
    case Message::T_STATE:
        gu_trace(handle_state(msg, um.source()));
        break;
    case Message::T_INSTALL:
        gu_trace(handle_install(msg, um.source()));
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
        handle_view(um.view());
    }
    else
    {
        Message msg;
        const gu::byte_t* b(gcomm::begin(rb));
        const size_t available(gcomm::available(rb));
        try
        {
            (void)msg.unserialize(b, available, 0);
        }
        catch (gu::Exception& e)
        {
            switch (e.get_errno())
            {
            case EPROTONOSUPPORT:
                if (prim() == false)
                {
                    gu_throw_fatal << e.what() << " terminating";
                }
                else
                {
                    log_warn << "unknown/unsupported protocol version: "
                             << msg.version()
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
            test_checksum(msg, rb, rb.offset());
        }

        try
        {
            handle_msg(msg, rb, um);
        }
        catch (gu::Exception& e)
        {
            log_error << "caught exception in PC, state dump to stderr follows:";
            std::cerr << *this << std::endl;
            throw;
        }
    }
}


int gcomm::pc::Proto::handle_down(Datagram& dg, const ProtoDownMeta& dm)
{
    if (gu_unlikely(state() != S_PRIM))
    {
        return EAGAIN;
    }
    if (gu_unlikely(dg.len() > mtu()))
    {
        return EMSGSIZE;
    }

    uint32_t    seq(dm.order() == O_SAFE ? last_sent_seq_ + 1 : last_sent_seq_);
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
        conf_.set(gcomm::Conf::PcIgnoreSb, value);
        return true;
    }
    else if (key == gcomm::Conf::PcIgnoreQuorum)
    {
        ignore_quorum_ = gu::from_string<bool>(value);
        conf_.set(gcomm::Conf::PcIgnoreQuorum, value);
        return true;
    }
    else if (key == gcomm::Conf::PcBootstrap)
    {
        if (state() != S_NON_PRIM)
        {
            log_info << "ignoring '" << key << "' in state "
                     << to_string(state());
        }
        else
        {
            send_install(true);
        }
        return true;
    }
    else if (key == Conf::PcChecksum ||
             key == Conf::PcAnnounceTimeout ||
             key == Conf::PcLinger ||
             key == Conf::PcNpvo ||
             key == Conf::PcWaitPrim ||
             key == Conf::PcWaitPrimTimeout ||
             key == Conf::PcWeight)
    {
        gu_throw_error(EPERM) << "can't change value for '"
                              << key << "' during runtime";
    }
    return false;
}
