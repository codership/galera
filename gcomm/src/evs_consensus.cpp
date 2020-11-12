/*
 * Copyright (C) 2009-2014 Codership Oy <info@codership.com>
 */

#include "evs_consensus.hpp"
#include "evs_message2.hpp"
#include "evs_input_map2.hpp"
#include "evs_node.hpp"
#include "evs_proto.hpp"
#include "gcomm/view.hpp"

#include "gu_logger.hpp"

#include <vector>

// Disable debug logging until debug mask is available here
#define evs_log_debug(i) if ((proto_.debug_mask_ & gcomm::evs::Proto::D_CONSENSUS) == 0) \
    {} else log_debug << proto_.uuid() << " "

//
// Helpers
//

class LeaveSeqCmpOp
{
public:

    bool operator()(const gcomm::evs::MessageNodeList::value_type& a,
                    const gcomm::evs::MessageNodeList::value_type& b) const
    {
        using gcomm::evs::MessageNode;
        using gcomm::evs::MessageNodeList;
        const MessageNode& aval(MessageNodeList::value(a));
        const MessageNode& bval(MessageNodeList::value(b));
        gcomm_assert(aval.leaving() != false &&
                     bval.leaving() != false);
        const gcomm::evs::seqno_t asec(aval.leave_seq());
        const gcomm::evs::seqno_t bsec(bval.leave_seq());
        gcomm_assert(asec != -1 && bsec != -1);
        return (asec < bsec);
    }
};


class RangeLuCmp
{
public:
    bool operator()(const gcomm::evs::MessageNodeList::value_type& a,
                    const gcomm::evs::MessageNodeList::value_type& b) const
    {
        return (gcomm::evs::MessageNodeList::value(a).im_range().lu() <
                gcomm::evs::MessageNodeList::value(b).im_range().lu());
    }
};


class SafeSeqCmp
{
public:
    bool operator()(const gcomm::evs::MessageNodeList::value_type& a,
                    const gcomm::evs::MessageNodeList::value_type& b) const
    {
        return (gcomm::evs::MessageNodeList::value(a).safe_seq() <
                gcomm::evs::MessageNodeList::value(b).safe_seq());
    }
};


//
//
//


bool gcomm::evs::Consensus::equal(const Message& m1, const Message& m2) const
{
    gcomm_assert(m1.type() == Message::EVS_T_JOIN ||
                 m1.type() == Message::EVS_T_INSTALL);
    gcomm_assert(m2.type() == Message::EVS_T_JOIN ||
                 m2.type() == Message::EVS_T_INSTALL);

    // Seq and aru seq are comparable only if coming from same view
    if (m1.source_view_id() == m2.source_view_id())
    {
        if (m1.seq() != m2.seq())
        {
            evs_log_debug(D_CONSENSUS) << "seq not equal " <<
                m1.seq() << " " << m2.seq();
            return false;
        }
        if (m1.aru_seq() != m2.aru_seq())
        {
            evs_log_debug(D_CONSENSUS) << "aruseq not equal " <<
                m1.aru_seq() << " " << m2.aru_seq();
            return false;
        }
    }

    MessageNodeList nl1, nl2;

    // When comparing messages from same source whole node list is comparable,
    // otherwise only operational part of it.
    if (m1.source() == m2.source())
    {
        for_each(m1.node_list().begin(), m1.node_list().end(),
                 SelectNodesOp(nl1, m1.source_view_id(), true, true));
        for_each(m2.node_list().begin(), m2.node_list().end(),
                 SelectNodesOp(nl2, m2.source_view_id(), true, true));
    }
    else
    {
        for_each(m1.node_list().begin(), m1.node_list().end(),
                 SelectNodesOp(nl1, ViewId(), true, false));
        for_each(m2.node_list().begin(), m2.node_list().end(),
                 SelectNodesOp(nl2, ViewId(), true, false));
    }

    evs_log_debug(D_CONSENSUS) << "nl1: " << nl1 << " nl2: " << nl2;

    return (nl1 == nl2);
}


gcomm::evs::seqno_t gcomm::evs::Consensus::highest_reachable_safe_seq() const
{
    std::vector<seqno_t> seq_list;
    seq_list.reserve(known_.size());

    for (NodeMap::const_iterator i = known_.begin(); i != known_.end();
         ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        const JoinMessage* jm(node.join_message());
        const LeaveMessage* lm(node.leave_message());

        if ((jm == 0 && current_view_.is_member(NodeMap::key(i)) == true) ||
            (jm != 0 && jm->source_view_id() == current_view_.id()) ||
            (lm != 0 && lm->source_view_id() == current_view_.id()))
        {
            if (lm != 0)
            {
                if (proto_.is_all_suspected(uuid) == false)
                {
                    seq_list.push_back(lm->seq());
                }
            }
            else if (node.operational() == false)
            {
                seq_list.push_back(
                    std::min(
                        input_map_.safe_seq(node.index()),
                        input_map_.range(node.index()).lu() - 1));
            }
            else
            {
                seq_list.push_back(input_map_.range(node.index()).hs());
            }
        }
    }

    return *std::min_element(seq_list.begin(), seq_list.end());
}

gcomm::evs::seqno_t
gcomm::evs::Consensus::safe_seq_wo_all_susupected_leaving_nodes() const
{
    seqno_t safe_seq(-2);
    for(NodeMap::const_iterator i = proto_.known_.begin();
        i != proto_.known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        if (node.index() != std::numeric_limits<size_t>::max()) {
            if (node.operational() == false &&
                node.leave_message() &&
                proto_.is_all_suspected(uuid)) {
                continue;
            }
            seqno_t ss = input_map_.safe_seq(node.index());
            if (safe_seq == -2 ||
                ss < safe_seq) {
                safe_seq = ss;
            }
        }
    }
    return safe_seq;
}

namespace gcomm {
namespace evs {

class FilterAllSuspectedOp
{
public:
    FilterAllSuspectedOp(MessageNodeList& nl,
                         const Proto& proto)
            :
            nl_(nl), proto_(proto) {}
    void operator()(const MessageNodeList::value_type& vt) const
    {
        const UUID& uuid(MessageNodeList::key(vt));
        if (!proto_.is_all_suspected(uuid)) {
            nl_.insert_unique(vt);
        }
    }
private:
    MessageNodeList& nl_;
    const Proto& proto_;
};

} // evs
} // gcomm

bool gcomm::evs::Consensus::is_consistent_highest_reachable_safe_seq(
    const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);
    gcomm_assert(msg.source_view_id() == current_view_.id());

    const MessageNodeList& node_list(msg.node_list());

    // Same view
    MessageNodeList same_view;
    for_each(node_list.begin(), node_list.end(),
             SelectNodesOp(same_view, current_view_.id(), true, false));
    MessageNodeList::const_iterator max_hs_i(max_element(same_view.begin(),
                                                         same_view.end(),
                                                         RangeHsCmp()));
    gcomm_assert(max_hs_i != same_view.end());

    // Max highest seen
    const seqno_t max_hs(
        MessageNodeList::value(max_hs_i).im_range().hs());

    seqno_t max_reachable_safe_seq(max_hs);

    // Leaving Nodes
    MessageNodeList t_leaving;
    for_each(node_list.begin(), node_list.end(),
             SelectNodesOp(t_leaving, current_view_.id(), false, true));
    MessageNodeList leaving;
    for_each(t_leaving.begin(), t_leaving.end(),
             FilterAllSuspectedOp(leaving, proto_));

    if (leaving.empty() == false)
    {
        const MessageNodeList::const_iterator min_leave_seq_i(
            std::min_element(leaving.begin(), leaving.end(),
                        LeaveSeqCmpOp()));
        gcomm_assert(min_leave_seq_i != leaving.end());
        const seqno_t min_leave_seq(
            MessageNodeList::value(min_leave_seq_i).leave_seq());
        max_reachable_safe_seq = std::min(max_reachable_safe_seq, min_leave_seq);
    }

    // Partitioning nodes
    MessageNodeList partitioning;
    for_each(node_list.begin(), node_list.end(),
             SelectNodesOp(partitioning, current_view_.id(), false, false));

    if (partitioning.empty() == false)
    {
        MessageNodeList::const_iterator min_part_safe_seq_i(
            std::min_element(partitioning.begin(), partitioning.end(),
                        SafeSeqCmp()));
        gcomm_assert(min_part_safe_seq_i != partitioning.end());
        const seqno_t min_part_safe_seq(
            MessageNodeList::value(min_part_safe_seq_i).safe_seq());
        max_reachable_safe_seq = std::min(max_reachable_safe_seq,
                                          min_part_safe_seq);

        MessageNodeList::const_iterator min_part_lu_i(
            std::min_element(partitioning.begin(), partitioning.end(),
                             RangeLuCmp()));
        gcomm_assert(min_part_lu_i != partitioning.end());
        const seqno_t min_part_lu(MessageNodeList::value(min_part_lu_i).im_range().lu() - 1);
        max_reachable_safe_seq = std::min(max_reachable_safe_seq,
                                          min_part_lu);
    }

    evs_log_debug(D_CONSENSUS)
        << " max reachable safe seq " << max_reachable_safe_seq
        << " highest reachable safe seq " << highest_reachable_safe_seq()
        << " max_hs " << max_hs
        << " input map max hs " << input_map_.max_hs()
        << " input map safe_seq " << input_map_.safe_seq()
        << " safe seq wo suspected leaving nodes " << safe_seq_wo_all_susupected_leaving_nodes();

    return (input_map_.max_hs()       == max_hs                 &&
            highest_reachable_safe_seq() == max_reachable_safe_seq &&
            // input_map_.safe_seq()     == max_reachable_safe_seq);
            safe_seq_wo_all_susupected_leaving_nodes() == max_reachable_safe_seq);
}


bool gcomm::evs::Consensus::is_consistent_input_map(const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);
    gcomm_assert(msg.source_view_id() == current_view_.id());


    if (msg.aru_seq() != input_map_.aru_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message aru seq "
                                   << msg.aru_seq()
                                   << " not consistent with input map aru seq "
                                   << input_map_.aru_seq();
        return false;
    }

    if (msg.seq() != input_map_.safe_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message safe seq "
                                   << msg.seq()
                                   << " not consistent with input map safe seq "
                                   << input_map_.safe_seq();
        return false;
    }

    Map<const UUID, Range> local_insts, msg_insts;

    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        if (current_view_.is_member(uuid) == true)
        {
            gu_trace((void)local_insts.insert_unique(
                         std::make_pair(uuid, input_map_.range(node.index()))));
        }
    }

    const MessageNodeList& m_insts(msg.node_list());

    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& msg_uuid(MessageNodeList::key(i));
        const MessageNode& msg_inst(MessageNodeList::value(i));
        if (msg_inst.view_id() == current_view_.id())
        {
            gu_trace((void)msg_insts.insert_unique(
                         std::make_pair(msg_uuid, msg_inst.im_range())));
        }
    }

    evs_log_debug(D_CONSENSUS) << " msg_insts " << msg_insts
                               << " local_insts " << local_insts;

    return (msg_insts == local_insts);
}


bool gcomm::evs::Consensus::is_consistent_partitioning(const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);
    gcomm_assert(msg.source_view_id() == current_view_.id());


    // Compare instances that were present in the current view but are
    // not proceeding in the next view.

    Map<const UUID, Range> local_insts, msg_insts;

    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& node(NodeMap::value(i));
        if (node.operational()       == false &&
            node.leave_message()     == 0     &&
            current_view_.is_member(uuid) == true)
        {
            gu_trace((void)local_insts.insert_unique(
                         std::make_pair(uuid,
                                        input_map_.range(node.index()))));
        }
    }

    const MessageNodeList& m_insts = msg.node_list();

    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::key(i));
        const MessageNode& m_inst(MessageNodeList::value(i));
        if (m_inst.operational() == false &&
            m_inst.leaving()     == false &&
            m_inst.view_id()     == current_view_.id())
        {
            gu_trace((void)msg_insts.insert_unique(
                         std::make_pair(m_uuid, m_inst.im_range())));
        }
    }


    evs_log_debug(D_CONSENSUS) << " msg insts:\n" << msg_insts
                               << " local insts:\n" << local_insts;
    return (msg_insts == local_insts);
}


bool gcomm::evs::Consensus::is_consistent_leaving(const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);
    gcomm_assert(msg.source_view_id() == current_view_.id());

    // Compare instances that were present in the current view but are
    // not proceeding in the next view.

    Map<const UUID, Range> local_insts, msg_insts;

    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const UUID& uuid(NodeMap::key(i));
        const Node& inst(NodeMap::value(i));
        const LeaveMessage* lm(inst.leave_message());

        if (inst.operational()   == false &&
            lm                       != 0  &&
            lm->source_view_id() == current_view_.id())
        {
            gu_trace((void)local_insts.insert_unique(
                         std::make_pair(uuid, input_map_.range(inst.index()))));
        }
    }

    const MessageNodeList& m_insts = msg.node_list();

    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::key(i));
        const MessageNode& m_inst(MessageNodeList::value(i));
        if (m_inst.operational() == false &&
            m_inst.leaving()     == true &&
            m_inst.view_id()     == current_view_.id())
        {
            gu_trace((void)msg_insts.insert_unique(
                         std::make_pair(m_uuid, m_inst.im_range())));
        }
    }

    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;
    return (local_insts == msg_insts);
}


bool gcomm::evs::Consensus::is_consistent_same_view(const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);
    gcomm_assert(msg.source_view_id() == current_view_.id());

    if (is_consistent_highest_reachable_safe_seq(msg) == false)
    {
        evs_log_debug(D_CONSENSUS)
            << "highest reachable safe seq not consistent";
        return false;
    }

    if (is_consistent_input_map(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "input map not consistent with " << msg;
        return false;
    }

    if (is_consistent_partitioning(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "partitioning not consistent with " << msg;
        return false;
    }

    if (is_consistent_leaving(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "leaving not consistent with " << msg;
        return false;
    }

    return true;
}


bool gcomm::evs::Consensus::is_consistent(const Message& msg) const
{
    gcomm_assert(msg.type() == Message::EVS_T_JOIN ||
                 msg.type() == Message::EVS_T_INSTALL);

    const JoinMessage* my_jm =
        NodeMap::value(known_.find_checked(proto_.uuid())).join_message();
    if (my_jm == 0)
    {
        return false;
    }
    if (msg.source_view_id() == current_view_.id())
    {
        return (is_consistent_same_view(msg) == true &&
                equal(msg, *my_jm) == true);
    }
    else
    {
        return equal(msg, *my_jm);
    }
}

bool gcomm::evs::Consensus::is_consensus() const
{
    const JoinMessage* my_jm =
        NodeMap::value(known_.find_checked(proto_.uuid())).join_message();

    if (my_jm == 0)
    {
        evs_log_debug(D_CONSENSUS) << "no own join message";
        return false;
    }

    if (is_consistent_same_view(*my_jm) == false)
    {
        evs_log_debug(D_CONSENSUS) << "own join message not consistent";
        return false;
    }

    for (NodeMap::const_iterator i = known_.begin(); i != known_.end(); ++i)
    {
        const Node& inst(NodeMap::value(i));
        if (inst.operational() == true)
        {
            const JoinMessage* jm = inst.join_message();
            if (jm == 0)
            {
                evs_log_debug(D_CONSENSUS)
                    << "no join message for " << NodeMap::key(i);
                return false;
            }
            // call is_consistent() instead of equal() to enforce strict
            // check for messages originating from the same view (#541)
            if (is_consistent(*jm) == false)
            {
                evs_log_debug(D_CONSENSUS)
                    << "join message " << *jm
                    << " not consistent with my join " << *my_jm;
                return false;
            }
        }
    }

    return true;
}
