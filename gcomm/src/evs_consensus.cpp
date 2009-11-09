/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "evs_consensus.hpp"
#include "evs_message2.hpp"
#include "evs_input_map2.hpp"
#include "evs_node.hpp"
#include "gcomm/view.hpp"

#include <list>

using namespace std;
using namespace gcomm;
using namespace gcomm::evs;

// Disable debug logging until debug mask is available here
#define evs_log_debug(int) if (true) {} else log_debug


//
// Helpers
// 

class LeaveSeqCmpOp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        const MessageNode& aval(MessageNodeList::get_value(a));
        const MessageNode& bval(MessageNodeList::get_value(b));
        gcomm_assert(aval.get_leaving() != false &&
                     bval.get_leaving() != false);
        const Seqno asec(aval.get_leave_seq());
        const Seqno bsec(bval.get_leave_seq());
        gcomm_assert(asec != Seqno::max() && bsec != Seqno::max());
        return (asec < bsec);
    }
};


class RangeLuCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_im_range().get_lu() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_im_range().get_lu() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_im_range().get_lu() < 
                MessageNodeList::get_value(b).get_im_range().get_lu();
        }
    }
};


class SafeSeqCmp
{
public:
    bool operator()(const MessageNodeList::value_type& a,
                    const MessageNodeList::value_type& b) const
    {
        if (MessageNodeList::get_value(a).get_safe_seq() == Seqno::max())
        {
            return true;
        }
        else if (MessageNodeList::get_value(b).get_safe_seq() == Seqno::max())
        {
            return false;
        }
        else
        {
            return MessageNodeList::get_value(a).get_safe_seq() < 
                MessageNodeList::get_value(b).get_safe_seq();
        }
    }
};


//
//
//

gcomm::evs::Seqno gcomm::evs::Consensus::highest_reachable_safe_seq() const
{
    list<Seqno> seq_list;
    for (NodeMap::const_iterator i = known.begin(); i != known.end();
         ++i)
    {
        const Node& node(NodeMap::get_value(i));
        const JoinMessage* jm(node.get_join_message());
        const LeaveMessage* lm(node.get_leave_message());
        if ((jm == 0 && current_view.is_member(NodeMap::get_key(i)) == true) ||
            (jm != 0 && jm->get_source_view_id() == current_view.get_id()))
        {
            if (node.get_operational() == false || lm != 0)
            {
                const Seqno max_reachable_safe_seq(
                    input_map.get_safe_seq(node.get_index()));
                if (lm == 0 && max_reachable_safe_seq != Seqno::max())
                {
                    seq_list.push_back(max_reachable_safe_seq);
                }
                else if (lm != 0)
                {
                    gcomm_assert(lm->get_seq() != Seqno::max());
                    seq_list.push_back(lm->get_seq());
                }
                else
                {
                    return Seqno::max();
                }
            }
            else
            {
                const Seqno im_hs(input_map.get_range(node.get_index()).get_hs());
                if (im_hs != Seqno::max())
                {
                    seq_list.push_back(im_hs);
                }
                else
                {
                    return Seqno::max();
                }
            }
        }
    }
    
    gcomm_assert(seq_list.empty() == false);
    
    return *min_element(seq_list.begin(), seq_list.end());
}


bool gcomm::evs::Consensus::is_consistent_highest_reachable_safe_seq(
    const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN || 
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    const MessageNodeList& node_list(msg.get_node_list());
    
    // Same view
    MessageNodeList same_view;
    for_each(node_list.begin(), node_list.end(),
             SelectNodesOp(same_view, current_view.get_id(), true, true));
    MessageNodeList::const_iterator max_hs_i(max_element(same_view.begin(), 
                                                         same_view.end(), 
                                                         RangeHsCmp()));
    // Max highest seen
    const Seqno max_hs(
        max_hs_i == same_view.end() ? 
        Seqno::max() : 
        MessageNodeList::get_value(max_hs_i).get_im_range().get_hs());
    
    Seqno max_reachable_safe_seq(max_hs);
    
    MessageNodeList leaving;
    for_each(node_list.begin(), node_list.end(), 
             SelectNodesOp(leaving, current_view.get_id(), false, true));
    
    if (leaving.empty() == false)
    {
        const MessageNodeList::const_iterator min_leave_seq_i(
            min_element(leaving.begin(), leaving.end(),
                        LeaveSeqCmpOp()));
        gcomm_assert(min_leave_seq_i != leaving.end());
        const Seqno min_leave_seq(
            MessageNodeList::get_value(min_leave_seq_i).get_leave_seq());
        gcomm_assert(min_leave_seq != Seqno::max());
        if (max_reachable_safe_seq == Seqno::max())
        {
            // We will always get at least this far
            max_reachable_safe_seq = min_leave_seq;
        }
        else
        {
            max_reachable_safe_seq = min(max_reachable_safe_seq, 
                                         min_leave_seq);
        }
    }
    
    MessageNodeList partitioning;
    for_each(node_list.begin(), node_list.end(), 
             SelectNodesOp(partitioning, current_view.get_id(), false, false));
    
    if (partitioning.empty() == false)
    {
        MessageNodeList::const_iterator min_part_safe_seq_i(
            min_element(partitioning.begin(), partitioning.end(),
                        SafeSeqCmp()));
        const Seqno min_part_safe_seq(
            min_part_safe_seq_i == partitioning.end() ?
            Seqno::max() : 
            MessageNodeList::get_value(min_part_safe_seq_i).get_safe_seq());
        if (min_part_safe_seq      != Seqno::max() &&
            max_reachable_safe_seq != Seqno::max())
        {
            max_reachable_safe_seq = min(max_reachable_safe_seq, 
                                         min_part_safe_seq);
        }
        else
        {
            max_reachable_safe_seq = Seqno::max();
        }
    }
    
    evs_log_debug(D_CONSENSUS)
        << " max reachable safe seq " << max_reachable_safe_seq
        << " highest reachable safe seq " << highest_reachable_safe_seq()
        << " max_hs " << max_hs 
        << " input map max hs " << input_map.get_max_hs()
        << " input map safe_seq " << input_map.get_safe_seq();
    
    return (input_map.get_max_hs()      == max_hs                 &&
            highest_reachable_safe_seq() == max_reachable_safe_seq &&
            input_map.get_safe_seq()    == max_reachable_safe_seq);
}

bool gcomm::evs::Consensus::is_consistent_input_map(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN || 
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    
    if (msg.get_aru_seq() != input_map.get_aru_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message aru seq "
                                   << msg.get_aru_seq()
                                   << " not consistent with input map aru seq "
                                   << input_map.get_aru_seq();
        return false;
    }
    
    if (msg.get_seq() != input_map.get_safe_seq())
    {
        evs_log_debug(D_CONSENSUS) << "message safe seq "
                                   << msg.get_seq()
                                   << " not consistent with input map safe seq "
                                   << input_map.get_safe_seq();
        return false;
    }
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& node(NodeMap::get_value(i));
        if (current_view.is_member(uuid) == true)
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, input_map.get_range(node.get_index()))));
        }
    }
    
    const MessageNodeList& m_insts(msg.get_node_list());
    
    for (MessageNodeList::const_iterator i = m_insts.begin();
         i != m_insts.end(); ++i)
    {
        const UUID& msg_uuid(MessageNodeList::get_key(i));
        const MessageNode& msg_inst(MessageNodeList::get_value(i));
        if (msg_inst.get_view_id() == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(msg_uuid, msg_inst.get_im_range())));
        }
    }
    
    evs_log_debug(D_CONSENSUS) << " msg_insts " << msg_insts
                               << " local_insts " << local_insts;
    
    return (msg_insts == local_insts);
}

bool gcomm::evs::Consensus::is_consistent_partitioning(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // Compare instances that were present in the current view but are 
    // not proceeding in the next view.
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& node(NodeMap::get_value(i));
        if (node.get_operational()       == false &&
            node.get_leave_message()     == 0     &&
            current_view.is_member(uuid) == true)
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, 
                                   input_map.get_range(node.get_index()))));
        }
    }
    
    gcomm_assert(msg.has_node_list() == true);
    const MessageNodeList& m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator i = m_insts.begin(); 
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::get_key(i));
        const MessageNode& m_inst(MessageNodeList::get_value(i));
        if (m_inst.get_operational() == false &&
            m_inst.get_leaving()     == false &&
            m_inst.get_view_id()     == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(m_uuid, m_inst.get_im_range())));
        }
    }

    
    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;
    return (msg_insts == local_insts);
}

bool gcomm::evs::Consensus::is_consistent_leaving(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    // Compare instances that were present in the current view but are 
    // not proceeding in the next view.
    
    Map<const UUID, Range> local_insts, msg_insts;
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        const LeaveMessage* lm(inst.get_leave_message());
        
        if (inst.get_operational()   == false &&
            lm                       != 0  &&
            lm->get_source_view_id() == current_view.get_id())
        {
            gu_trace((void)local_insts.insert_checked(
                         make_pair(uuid, input_map.get_range(inst.get_index()))));
        }
    }
    
    const MessageNodeList& m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator i = m_insts.begin(); 
         i != m_insts.end(); ++i)
    {
        const UUID& m_uuid(MessageNodeList::get_key(i));
        const MessageNode& m_inst(MessageNodeList::get_value(i));
        if (m_inst.get_operational() == false &&
            m_inst.get_leaving()     == true &&
            m_inst.get_view_id()     == current_view.get_id())
        {
            gu_trace((void)msg_insts.insert_checked(
                         make_pair(m_uuid, m_inst.get_im_range())));
        }
    }

    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;    
    return (local_insts == msg_insts);
}


bool gcomm::evs::Consensus::is_consistent_same_view(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(msg.get_source_view_id() == current_view.get_id());
    
    if (is_consistent_highest_reachable_safe_seq(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) 
            << "highest reachable safe seq not consistent";
        return false;
    }
    
    if (is_consistent_input_map(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "input map not consistent ";
        return false;
    }
    
    if (is_consistent_partitioning(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "partitioning not consistent";
        return false;
    }
    
    if (is_consistent_leaving(msg) == false)
    {
        evs_log_debug(D_CONSENSUS) << "leaving not consistent";
        return false;
    }
    
    return true;
}


bool gcomm::evs::Consensus::is_consistent_joining(const Message& msg) const
{
    gcomm_assert(msg.get_type() == Message::T_JOIN ||
                 msg.get_type() == Message::T_INSTALL);
    gcomm_assert(current_view.get_id() != msg.get_source_view_id());
    
    Map<const UUID, Range> local_insts, msg_insts;
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const UUID& uuid(NodeMap::get_key(i));
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == false)
        {
            continue;
        }
        
        const JoinMessage* jm = inst.get_join_message();        
        if (jm == 0)
        {
            if (msg.get_source() == get_uuid())
            {
                evs_log_debug(D_CONSENSUS) 
                    << "own join not consistent with joining";
            }
            return false;
        }
        
        if (msg.get_source_view_id() == jm->get_source_view_id())
        { 
            if (msg.get_aru_seq() != jm->get_aru_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    evs_log_debug(D_CONSENSUS) << "own join not consistent with joining jm aru seq";
                }
                return false;
            }
            if (msg.get_seq() != jm->get_seq())
            {
                if (msg.get_source() == get_uuid())
                {
                    evs_log_debug(D_CONSENSUS) << "own join not consistent with joining jm seq";
                }
                return false;
            }
        }
        gu_trace((void)local_insts.insert_checked(make_pair(uuid, Range())));
    }
    
    assert(msg.has_node_list() == true);
    const MessageNodeList m_insts = msg.get_node_list();
    
    for (MessageNodeList::const_iterator mi = m_insts.begin();
         mi != m_insts.end(); ++mi)
    {
        const UUID& m_uuid(MessageNodeList::get_key(mi));
        const MessageNode& m_inst(MessageNodeList::get_value(mi));
        
        if (m_inst.get_operational() == true)
        {
            gu_trace((void)msg_insts.insert_checked(make_pair(m_uuid, Range())));
        }
    }
    
    evs_log_debug(D_CONSENSUS) << " msg insts " << msg_insts
                               << " local insts " << local_insts;    
    
    return (local_insts == msg_insts);
}



bool gcomm::evs::Consensus::is_consistent(const Message& msg) const
{
    if (msg.get_source_view_id() == current_view.get_id())
    {
        return is_consistent_same_view(msg);
    }
    else
    {
        return is_consistent_joining(msg);
    }
}



bool gcomm::evs::Consensus::is_consensus() const
{
    const JoinMessage* my_jm = 
        NodeMap::get_value(known.find_checked(get_uuid())).get_join_message();
    
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
    
    for (NodeMap::const_iterator i = known.begin(); i != known.end(); ++i)
    {
        const Node& inst(NodeMap::get_value(i));
        if (inst.get_operational() == false)
        {
            continue;
        }
        
        const JoinMessage* jm = inst.get_join_message();
        if (jm == 0)
        {
            evs_log_debug(D_CONSENSUS) 
                << "no join message for " << NodeMap::get_key(i);
            return false;
        }
        
        if (is_consistent(*jm) == false)
        {
            return false;
        }
    }
#if 0
    if (install_message == 0)
    {
        evs_log_debug(D_CONSENSUS) << "consensus reached";
    }
    if (is_representative(get_uuid()) == true && install_message == 0)
    {
        evs_log_debug(D_CONSENSUS) << "consensus state " << *this;
    }
#endif
    return true;
}
