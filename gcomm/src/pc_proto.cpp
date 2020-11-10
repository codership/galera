/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "pc_proto.hpp"
#include "pc_message.hpp"

#include "gcomm/util.hpp"

#include "gu_lock.hpp"
#include "gu_logger.hpp"
#include "gu_macros.h"
#include <algorithm>
#include <set>

#include <boost/bind.hpp>

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

class UUIDFixedPartCmp
{
public:
    UUIDFixedPartCmp(const gcomm::UUID& uuid) : uuid_(uuid) { }
    bool operator()(const gcomm::NodeList::value_type& vt) const
    {
        return uuid_.fixed_part_matches(vt.first);
    }
private:
    const gcomm::UUID& uuid_;
};

static bool UUID_fixed_part_cmp_equal(const gcomm::NodeList::value_type& lhs,
                                      const gcomm::NodeList::value_type& rhs)
{
    return lhs.first.fixed_part_matches(rhs.first);
}

static bool UUID_fixed_part_cmp_intersection(const gcomm::UUID& lhs,
                                             const gcomm::UUID& rhs)
{
    return lhs.fixed_part_matches(rhs) ? false : lhs < rhs;
}

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

    StateMessage pcs(current_view_.version());

    NodeMap& im(pcs.node_map());

    for (NodeMap::iterator i = instances_.begin(); i != instances_.end(); ++i)
    {
        // Assume all nodes in the current view have reached current to_seq
        Node& local_state(NodeMap::value(i));
        if (current_view_.is_member(NodeMap::key(i)) == true)
        {
            local_state.set_to_seq(to_seq());
        }
        if (is_evicted(NodeMap::key(i)) == true)
        {
            local_state.set_evicted(true);
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

int gcomm::pc::Proto::send_install(bool bootstrap, int weight)
{
    gcomm_assert(bootstrap == false || weight == -1);
    log_debug << self_id() << " send install";

    InstallMessage pci(current_view_.version());

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
    else if (weight != -1)
    {
        pci.flags(pci.flags() | InstallMessage::F_WEIGHT_CHANGE);
        Node& self(pci.node(uuid()));
        self.set_weight(weight);
        log_info << self_id() << " sending PC weight change message " << pci;
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
    return ret;
}


void gcomm::pc::Proto::deliver_view(bool bootstrap)
{
    View v(pc_view_.version(), pc_view_.id(), bootstrap);

    for (NodeMap::const_iterator i = instances_.begin();
         i != instances_.end(); ++i)
    {
        if (current_view_.members().find(NodeMap::key(i)) ==
            current_view_.members().end())
        {
            v.add_partitioned(NodeMap::key(i), NodeMap::value(i).segment());
        }
        else
        {
            v.add_member(NodeMap::key(i), NodeMap::value(i).segment());
        }
    }

    ProtoUpMeta um(UUID::nil(), ViewId(), &v);
    log_info << v;
    send_up(Datagram(), um);
    set_stable_view(v);

    if (v.id().type() == V_NON_PRIM &&
        rst_view_ && !start_prim_) {
        // pc recovery process.
        uint32_t max_view_seqno = 0;
        bool check = true;
        for(NodeMap::const_iterator i = instances_.begin();
            i != instances_.end(); ++i) {
            const UUID& uuid(NodeMap::key(i));
            // just consider property of nodes in restored view.
            if (std::find_if(rst_view_->members().begin(),
                             rst_view_->members().end(),
                             UUIDFixedPartCmp(uuid))
                != rst_view_->members().end())
            {
                const Node& node(NodeMap::value(i));
                const ViewId& last_prim(node.last_prim());
                if (last_prim.type() != V_NON_PRIM ||
                    last_prim.uuid() != rst_view_ -> id().uuid()) {
                    log_warn << "node uuid: " << uuid << " last_prim(type: "
                             << last_prim.type() << ", uuid: "
                             << last_prim.uuid() << ") is inconsistent to "
                             << "restored view(type: V_NON_PRIM, uuid: "
                             << rst_view_ ->id().uuid();
                    check = false;
                    break;
                }
                max_view_seqno = std::max(max_view_seqno,
                                          last_prim.seq());
            }
        }
        if (check) {
            assert(max_view_seqno != 0);
            log_debug << "max_view_seqno = " << max_view_seqno
                      << ", rst_view_seqno = " << rst_view_ -> id().seq();
            log_debug << "rst_view = ";
            log_debug << *rst_view_;
            log_debug << "deliver_view = ";
            log_debug << v;

            if (rst_view_->id().seq() == max_view_seqno &&
                v.members().size() == rst_view_->members().size() &&
                std::equal(v.members().begin(), v.members().end(),
                           rst_view_->members().begin(), UUID_fixed_part_cmp_equal))
            {
                log_info << "promote to primary component";
                // All of the nodes are in non-primary so we need to bootstrap.
                send_install(true);
                // Rst_view will be cleared after primary component is formed.
                // If the rst_view would be cleared here and there would be
                // network partitioning before install message was delivered,
                // bootstrapping the primary component would never happen again.
            }
        }
    }

    // if pc is formed by normal process(start_prim_=true) instead of
    // pc recovery process, rst_view_ won't be clear.
    // however this will prevent pc remerge(see is_prim function)
    // so we have to clear rst_view_ once pc is formed..
    if (v.id().type() == V_PRIM &&
        rst_view_) {
        log_info << "clear restored view";
        rst_view_ = NULL;
    }
}


void gcomm::pc::Proto::mark_non_prim()
{
    pc_view_ = View(current_view_.version(),
                    ViewId(V_NON_PRIM, current_view_.id()));
    for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
         ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        Node& inst(NodeMap::value(i));
        if (current_view_.members().find(uuid) != current_view_.members().end())
        {
            inst.set_prim(false);
            pc_view_.add_member(uuid, inst.segment());
        }
    }

    set_prim(false);

}

void gcomm::pc::Proto::shift_to(const State s)
{
    // State graph
    static const bool allowed[S_MAX][S_MAX] = {

        // Cl     S-E    IN     P      Trans  N-P
        {  false, false, false, false, false, true  }, // Closed
        {  true,  false, true,  false, true,  true  }, // States exch
        {  true,  false, false, true,  true,  true  }, // Install
        {  true,  false, false, false, true,  true  }, // Prim
        {  true,  true,  false, false, false, true  }, // Trans
        {  true,  false, false,  true, true,  true  }  // Non-prim
    };



    if (allowed[state()][s] == false)
    {
        gu_throw_fatal << "Forbidden state transition: "
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
        pc_view_ = View(current_view_.version(),
                        ViewId(V_PRIM, current_view_.id()));
        for (NodeMap::iterator i = instances_.begin(); i != instances_.end();
             ++i)
        {
            const UUID& uuid(NodeMap::key(i));
            Node& inst(NodeMap::value(i));
            NodeList::const_iterator nli;
            if ((nli = current_view_.members().find(uuid)) !=
                current_view_.members().end())
            {
                inst.set_prim(true);
                inst.set_last_prim(ViewId(V_PRIM, current_view_.id()));
                inst.set_last_seq(0);
                inst.set_to_seq(to_seq());
                pc_view_.add_member(uuid, inst.segment());
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

static bool node_list_intersection_comp(const gcomm::NodeList::value_type& vt1,
                                        const gcomm::NodeList::value_type& vt2)
{
    return (vt1.first < vt2.first);
}

static gcomm::NodeList node_list_intersection(const gcomm::NodeList& nl1,
                                              const gcomm::NodeList& nl2)
{
    gcomm::NodeList ret;
    std::set_intersection(nl1.begin(), nl1.end(), nl2.begin(), nl2.end(),
                          std::inserter(ret, ret.begin()),
                          node_list_intersection_comp);
    return ret;
}

bool gcomm::pc::Proto::have_quorum(const View& view, const View& pc_view) const
{
    // Compare only against members and left which were part of the pc_view.
    gcomm::NodeList memb_intersection(
        node_list_intersection(view.members(), pc_view.members()));
    gcomm::NodeList left_intersection(
        node_list_intersection(view.left(), pc_view.members()));
    if (have_weights(view.members(), instances_) &&
        have_weights(view.left(), instances_)    &&
        have_weights(pc_view.members(), instances_))
    {
        return (weighted_sum(memb_intersection, instances_) * 2
                + weighted_sum(left_intersection, instances_) >
                weighted_sum(pc_view.members(), instances_));
    }
    else
    {
        return (memb_intersection.size()*2 + left_intersection.size() >
                pc_view.members().size());
    }
}


bool gcomm::pc::Proto::have_split_brain(const View& view) const
{
    // Compare only against members and left which were part of the pc_view.
    gcomm::NodeList memb_intersection(
        node_list_intersection(view.members(), pc_view_.members()));
    gcomm::NodeList left_intersection(
        node_list_intersection(view.left(), pc_view_.members()));
    if (have_weights(view.members(), instances_)  &&
        have_weights(view.left(), instances_)     &&
        have_weights(pc_view_.members(), instances_))
    {
        return (weighted_sum(memb_intersection, instances_) * 2
                + weighted_sum(left_intersection, instances_) ==
                weighted_sum(pc_view_.members(), instances_));
    }
    else
    {
        return (memb_intersection.size()*2 + left_intersection.size() ==
                pc_view_.members().size());
    }
}


void gcomm::pc::Proto::handle_trans(const View& view)
{
    gcomm_assert(view.id().type() == V_TRANS);
    gcomm_assert(view.id().uuid() == current_view_.id().uuid() &&
                 view.id().seq()  == current_view_.id().seq());
    gcomm_assert(view.version() == current_view_.version());

    log_debug << self_id() << " \n\n current view " << current_view_
              << "\n\n next view " << view
              << "\n\n pc view " << pc_view_;
    log_debug << *this;
    if (have_quorum(view, pc_view_) == false)
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

    if (current_view_.version() < view.version())
    {
        log_info << "PC protocol upgrade " << current_view_.version()
                 << " -> " << view.version();
    }
    else if (current_view_.version() > view.version())
    {
        log_info << "PC protocol downgrade " << current_view_.version()
                 << " -> " << view.version();
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


int gcomm::pc::Proto::cluster_weight() const
{
    int total_weight(0);
    if (pc_view_.type() == V_PRIM)
    {
        for (NodeMap::const_iterator i(instances_.begin());
             i != instances_.end(); ++i)
        {
            if (pc_view_.id() == i->second.last_prim())
            {
                total_weight += i->second.weight();
            }
        }
    }
    return total_weight;
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
                    if (msg_state.weight() == -1)
                    {
                        // backwards compatibility, ignore weight in state check
                        gcomm_assert(
                            msg_state.prim() == local_state.prim()     &&
                            msg_state.last_seq() == local_state.last_seq()  &&
                            msg_state.last_prim() == local_state.last_prim() &&
                            msg_state.to_seq()    == local_state.to_seq())
                            << self_id()
                            << " node " << uuid
                            << " prim state message and local states not consistent:"
                            << " msg node "   << msg_state
                            << " local state " << local_state;
                    }
                    else
                    {
                        gcomm_assert(msg_state == local_state)
                            << self_id()
                            << " node " << uuid
                            << " prim state message and local states not consistent:"
                            << " msg node "   << msg_state
                            << " local state " << local_state;
                    }
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
        else
        {
            // Clear unknow status from nodes in current view here.
            // New PC has been installed and if paritioning happens,
            // we either know for sure that the other partitioned component ends
            // up in non-prim, or in other case we have valid PC view to
            // deal with in case of remerge.
            NodeMap::value(i).set_un(false);
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
            log_info << "Node " << SMMap::key(i) << " state prim";
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

        // First determine if there are any nodes still in unknown state.
        std::set<UUID> un;
        for (NodeMap::const_iterator i(instances_.begin());
             i != instances_.end(); ++i)
        {
            if (NodeMap::value(i).un() == true &&
                current_view_.members().find(NodeMap::key(i)) ==
                current_view_.members().end())
            {
                un.insert(NodeMap::key(i));
            }
        }

        if (un.empty() == false)
        {
            std::ostringstream oss;
            std::copy(un.begin(), un.end(),
                      std::ostream_iterator<UUID>(oss, " "));
            log_info << "Nodes " << oss.str() << "are still in unknown state, "
                     << "unable to rebootstrap new prim";
            return false;
        }

        // Collect last prim members and evicted from state messages
        MultiMap<ViewId, UUID> last_prim_uuids;
        std::set<UUID> evicted;

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

                if (inst.last_prim().type() != V_NON_PRIM &&
                    std::find<MultiMap<ViewId, UUID>::iterator,
                              std::pair<const ViewId, UUID> >(
                                  last_prim_uuids.begin(),
                                  last_prim_uuids.end(),
                                  std::make_pair(inst.last_prim(), uuid)) ==
                    last_prim_uuids.end())
                {
                    last_prim_uuids.insert(std::make_pair(inst.last_prim(), uuid));
                }
                if (inst.evicted() == true)
                {
                    evicted.insert(uuid);
                }
            }
        }

        if (last_prim_uuids.empty() == true)
        {
            log_warn << "no nodes coming from prim view, prim not possible";
            return false;
        }

        // Construct greatest view set of UUIDs ignoring evicted ones
        std::set<UUID> greatest_view;
        // Get range of UUIDs in greatest views
        const ViewId greatest_view_id(last_prim_uuids.rbegin()->first);
        std::pair<MultiMap<ViewId, UUID>::const_iterator,
                  MultiMap<ViewId, UUID>::const_iterator> gvi =
            last_prim_uuids.equal_range(greatest_view_id);
        // Iterate over range and insert into greatest view if not evicted
        for (MultiMap<ViewId, UUID>::const_iterator i = gvi.first;
             i != gvi.second; ++i)
        {
            if (evicted.find(MultiMap<ViewId, UUID>::value(i)) == evicted.end())
            {
                std::pair<std::set<UUID>::iterator, bool>
                    iret = greatest_view.insert(
                        MultiMap<ViewId, UUID>::value(i));
                // Assert that inserted UUID was unique
                gcomm_assert(iret.second == true);
            }
        }
        log_debug << self_id()
                  << " greatest view id " << greatest_view_id;
        // Compute list of present view members
        std::set<UUID> present;
        for (NodeList::const_iterator i = current_view_.members().begin();
             i != current_view_.members().end(); ++i)
        {
            present.insert(NodeList::key(i));
        }
        // Compute intersection of present and greatest view. If the
        // intersection size is the same as greatest view size,
        // it is safe to rebootstrap PC.
        std::set<UUID> intersection;
        set_intersection(greatest_view.begin(), greatest_view.end(),
                         present.begin(), present.end(),
                         inserter(intersection, intersection.begin()),
                         UUID_fixed_part_cmp_intersection);
        log_debug << self_id()
                  << " intersection size " << intersection.size()
                  << " greatest view size " << greatest_view.size();

        if (intersection.size() == greatest_view.size())
        {
            log_info << "re-bootstrapping prim from partitioned components";
            prim = true;
        }
    }

    return prim;
}

void gcomm::pc::Proto::handle_state(const Message& msg, const UUID& source)
{
    gcomm_assert(msg.type() == Message::PC_T_STATE);
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
                const Node& sm_node(NodeMap::value(j));
                NodeMap::iterator local_node_i(instances_.find(sm_uuid));
                if (local_node_i == instances_.end())
                {
                    const Node& sm_state(NodeMap::value(j));
                    instances_.insert_unique(std::make_pair(sm_uuid, sm_state));
                }
                else
                {
                    Node& local_node(NodeMap::value(local_node_i));
                    if (local_node.weight() == -1 && sm_node.weight() != -1)
                    {
                        // backwards compatibility: override weight for
                        // instances which have been reported by old nodes
                        // but have weights associated anyway
                        local_node.set_weight(sm_node.weight());
                    }
                    else if (local_node.weight() != sm_node.weight() &&
                             SMMap::key(i) == NodeMap::key(local_node_i))
                    {
                        log_warn << self_id()
                                 << "overriding reported weight for "
                                 << NodeMap::key(local_node_i);
                        local_node.set_weight(sm_node.weight());
                    }
                    if (prim() == false && sm_node.un() == true &&
                        // note #92
                        local_node_i != self_i_)
                    {
                        // If coming from non-prim, set local instance status
                        // to unknown if any of the state messages has it
                        // marked unknown. If coming from prim, there is
                        // no need to set this as it is known if the node
                        // corresponding to local instance is in primary.
                        local_node.set_un(true);
                    }
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
    if (state() == S_PRIM)
    {
        if ((msg.flags() & Message::F_WEIGHT_CHANGE) == 0)
        {
            log_warn << "non weight changing install in S_PRIM: " << msg;
        }
        else
        {
            NodeMap::iterator local_i(instances_.find(source));
            const Node& msg_n(msg.node(source));
            log_info << self_id() << " changing node " << source
                     << " weight (reg) " << NodeMap::value(local_i).weight()
                     << " -> " << msg_n.weight();
            NodeMap::value(local_i).set_weight(msg_n.weight());
            if (source == uuid())
            {
                conf_.set(gcomm::Conf::PcWeight, gu::to_string(msg_n.weight()));
            }
        }
        return;
    }
    else if (state() == S_TRANS)
    {
        handle_trans_install(msg, source);
        return;
    }

    gcomm_assert(msg.type() == Message::PC_T_INSTALL);
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

    if (m_state.weight() == -1)
    {
        // backwards compatibility, ignore weight in state check
        const Node& self_state(NodeMap::value(self_i_));
        if ((m_state.prim()      == self_state.prim()     &&
             m_state.last_seq()  == self_state.last_seq()  &&
             m_state.last_prim() == self_state.last_prim() &&
             m_state.to_seq()    == self_state.to_seq()) == false)
        {
            gu_throw_fatal << self_id()
                           << "Install message self state does not match, "
                           << "message state: " << m_state
                           << ", local state: "
                           << NodeMap::value(self_i_);
        }
    }
    else
    {
        if (m_state != NodeMap::value(self_i_))
        {
            gu_throw_fatal << self_id()
                           << "Install message self state does not match, "
                           << "message state: " << m_state
                           << ", local state: "
                           << NodeMap::value(self_i_);
        }
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

namespace
{
    class ViewUUIDLT
    {
    public:
        bool operator()(const gcomm::NodeList::value_type& a,
                        const gcomm::NodeList::value_type& b) const
        {
            return (a.first < b.first);
        }
    };
}

// When delivering install message in trans view quorum has to be re-evaluated
// as the partitioned component may have installed prim view due to higher
// weight. To do this, we construct pc view that would have been installed
// if install message was delivered in reg view and make quorum computation
// against it.
//
// It is not actually known if partitioned component installed new PC, so
// we mark partitioned nodes states as unknown. This is to provide deterministic
// way to prevent automatic rebootstrapping of PC if some of the seen nodes
// is in unknown state.
void
gcomm::pc::Proto::handle_trans_install(const Message& msg, const UUID& source)
{
    gcomm_assert(msg.type() == Message::PC_T_INSTALL);
    gcomm_assert(state() == S_TRANS);
    gcomm_assert(current_view_.type() == V_TRANS);

    if ((msg.flags() & Message::F_BOOTSTRAP) != 0)
    {
        log_info << "Dropping bootstrap install in TRANS state";
        return;
    }

    gcomm_assert(have_quorum(current_view_, pc_view_) == true);

    if ((msg.flags() & Message::F_WEIGHT_CHANGE) != 0)
    {
        NodeList nl;
        nl.insert(current_view_.members().begin(), current_view_.members().end());
        nl.insert(current_view_.left().begin(), current_view_.left().end());

        if (std::includes(nl.begin(),
                          nl.end(),
                          pc_view_.members().begin(),
                          pc_view_.members().end(), ViewUUIDLT()) == false)
        {
            // Weight changing install message delivered in trans view
            // and previous pc view has partitioned.
            //
            // Need to be very conservative: We don't know what happened to
            // weight change message in partitioned component, so it may not be
            // safe to do quorum calculation. Shift to non-prim and
            // wait until partitioned component comes back (or prim is
            // rebootstrapped).
            //
            // It would be possible to do more fine grained decisions
            // based on the source of the message, but to keep things simple
            // always go to non-prim, this is very cornerish case after all.
            log_info << "Weight changing trans install leads to non-prim";
            mark_non_prim();
            deliver_view();
            for (NodeMap::const_iterator i(msg.node_map().begin());
                 i != msg.node_map().end(); ++i)
            {
                if (current_view_.members().find(NodeMap::key(i)) ==
                    current_view_.members().end())
                {
                    NodeMap::iterator local_i(instances_.find(NodeMap::key(i)));
                    if (local_i == instances_.end())
                    {
                        log_warn << "Node " << NodeMap::key(i)
                                 << " not found from instances";
                    }
                    else
                    {
                        if (NodeMap::key(i) == source)
                        {
                            NodeMap::value(local_i).set_weight(
                                NodeMap::value(i).weight());
                            if (source == uuid())
                            {
                                conf_.set(gcomm::Conf::PcWeight,
                                          gu::to_string(NodeMap::value(i).weight()));
                            }

                        }
                        NodeMap::value(local_i).set_un(true);
                    }
                }
            }
        }
        else
        {
            NodeMap::iterator local_i(instances_.find(source));
            const Node& msg_n(msg.node(source));
            log_info << self_id() << " changing node " << source
                     << " weight (trans) " << NodeMap::value(local_i).weight()
                     << " -> " << msg_n.weight();
            NodeMap::value(local_i).set_weight(msg_n.weight());
            if (source == uuid())
            {
                conf_.set(gcomm::Conf::PcWeight, gu::to_string(msg_n.weight()));
            }
        }
    }
    else
    {
        View new_pc_view(current_view_.version(),
                         ViewId(V_PRIM, current_view_.id()));
        for (NodeMap::iterator i(instances_.begin()); i != instances_.end();
             ++i)
        {
            const UUID& uuid(NodeMap::key(i));
            NodeMap::const_iterator ni(msg.node_map().find(uuid));
            if (ni != msg.node_map().end())
            {
                new_pc_view.add_member(uuid, 0);
            }
        }

        if (have_quorum(current_view_, new_pc_view) == false ||
            pc_view_.type() == V_NON_PRIM)
        {
            log_info << "Trans install leads to non-prim";
            mark_non_prim();
            deliver_view();
            // Mark all nodes in install msg node map but not in current
            // view with unknown status. It is not known if they delivered
            // install message in reg view and so formed new PC.
            for (NodeMap::const_iterator i(msg.node_map().begin());
                 i != msg.node_map().end(); ++i)
            {
                if (current_view_.members().find(NodeMap::key(i)) ==
                    current_view_.members().end())
                {
                    NodeMap::iterator local_i(instances_.find(NodeMap::key(i)));
                    if (local_i == instances_.end())
                    {
                        log_warn << "Node " << NodeMap::key(i)
                                 << " not found from instances";
                    }
                    else
                    {
                        NodeMap::value(local_i).set_un(true);
                    }
                }
            }
        }
    }
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
    // EVS provides send view delivery, so this assertion
    // should always hold.
    assert(msg.version() == current_view_.version());

    enum Verdict
    {
        ACCEPT,
        DROP,
        FAIL
    };

    static const Verdict verdicts[S_MAX][Message::PC_T_MAX] = {
        // Msg types
        // NONE,   STATE,   INSTALL,  USER
        {  FAIL,   FAIL,    FAIL,     FAIL    },  // Closed

        {  FAIL,   ACCEPT,  FAIL,     FAIL    },  // States exch

        {  FAIL,   FAIL,    ACCEPT,   FAIL    },  // INSTALL

        {  FAIL,   FAIL,    ACCEPT,   ACCEPT  },  // PRIM

        {  FAIL,   DROP,    ACCEPT,   ACCEPT  },  // TRANS

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
        log_debug << "Dropping input, message " << msg.to_string()
                  << " in state " << to_string(state());
        return;
    }

    switch (msg_type)
    {
    case Message::PC_T_STATE:
        gu_trace(handle_state(msg, um.source()));
        break;
    case Message::PC_T_INSTALL:
        gu_trace(handle_install(msg, um.source()));
        {
          gu::Lock lock(sync_param_mutex_);
          if (param_sync_set_ && (um.source() == uuid()))
          {
              param_sync_set_ = false;
              sync_param_cond_.signal();
          }
        }
        break;
    case Message::PC_T_USER:
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
    switch (state())
    {
    case S_CLOSED:
    case S_NON_PRIM:
        // Not connected to primary component
        return ENOTCONN;
    case S_STATES_EXCH:
    case S_INSTALL:
    case S_TRANS:
        // Transient error
        return EAGAIN;
    case S_PRIM:
        // Allowed to send, fall through
        break;
    case S_MAX:
        gu_throw_fatal << "invalid state " << state();
    }

    if (gu_unlikely(dg.len() > mtu()))
    {
        return EMSGSIZE;
    }

    uint32_t    seq(dm.order() == O_SAFE ? last_sent_seq_ + 1 : last_sent_seq_);
    UserMessage um(current_view_.version(), seq);

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

void gcomm::pc::Proto::sync_param()
{
    gu::Lock lock(sync_param_mutex_);

    while(param_sync_set_) 
    {
        lock.wait(sync_param_cond_);
    }
}

bool gcomm::pc::Proto::set_param(const std::string& key,
                                 const std::string& value,
                                 Protolay::sync_param_cb_t& sync_param_cb)
{
    bool ret;
    
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
            ret = send_install(true);
            if (ret != 0) gu_throw_error(ret); 
        }
        return true;
    }
    else if (key == gcomm::Conf::PcWeight)
    {
        if (state() != S_PRIM)
        {
            gu_throw_error(EAGAIN)
                << "can't change weightm: state not S_PRIM, retry again";
        }
        else
        {
            int w(gu::from_string<int>(value));
            if (w < 0 || w > 255)
            {
                gu_throw_error(ERANGE) << "value " << w << " for '" << key
                                       << "' out of range";
            }
            weight_ = w;
            {
                sync_param_cb = boost::bind(&gcomm::pc::Proto::sync_param, this);
                gu::Lock lock(sync_param_mutex_);
                param_sync_set_ = true;
            }
            ret = send_install(false, weight_);
            if (ret != 0) 
            { 
                gu::Lock lock(sync_param_mutex_);
                param_sync_set_ = false;
                gu_throw_error(ret);
            }
            return true;
        }
    }
    else if (key == Conf::PcChecksum ||
             key == Conf::PcAnnounceTimeout ||
             key == Conf::PcLinger ||
             key == Conf::PcNpvo ||
             key == Conf::PcWaitPrim ||
             key == Conf::PcWaitPrimTimeout ||
             key == Conf::PcRecovery)
    {
        gu_throw_error(EPERM) << "can't change value for '"
                              << key << "' during runtime";
    }
    return false;
}
