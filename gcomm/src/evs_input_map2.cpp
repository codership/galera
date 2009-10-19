/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "evs_input_map2.hpp"
#include "gcomm/readbuf.hpp"
#include "gcomm/safety_prefix.hpp"

#include <algorithm>

using namespace gcomm;
using namespace gcomm::evs;
using namespace std;

/*
 * Release ReadBuf associated to input map msg.
 */ 
static void release_rb(InputMap::MsgIndex::value_type& vt)
{
    ReadBuf* rb = InputMap::MsgIndex::get_value(vt).get_rb();
    if (rb != 0)
    {
        rb->release();
    }
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMapNode& in)
{
    os << "node: {";
    os << "idx=" << in.get_index() << ",";
    os << "range=" << in.get_range() << ",";
    os << "safe_seq=" << in.get_safe_seq();
    os << "}";
    return os;
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMapMsgKey& mk)
{
    return (os << "(" << mk.get_index() << "," << mk.get_seq() << ")");
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMapMsg& m)
{
    return os;
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMapMsgIndex::value_type& vt)
{
    return (os << "(" << InputMapMsgIndex::get_key(vt) << "," 
            << InputMapMsgIndex::get_value(vt) << ")");
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMap& im)
{
    os << "evs::input_map: {";
    os << "aru_seq=" << im.get_aru_seq() << ",";
    os << "safe_seq=" << im.get_safe_seq() << ",";
    os << "node_index=" << *im.node_index << ",";
    os << "msg_index=" << *im.msg_index << ",";
    os << "recovery_index=" << *im.recovery_index << ",";
    os << "inserted=" << im.inserted << ",";
    os << "updated_aru=" << im.updated_aru << "}";
    return os;
}


gcomm::evs::InputMap::InputMap() :
    safe_seq(Seqno::max()),
    aru_seq(Seqno::max()),
    node_index(new NodeIndex()),
    msg_index(new MsgIndex()),
    recovery_index(new MsgIndex()),
    inserted(0),
    updated_aru(0)
{
}


gcomm::evs::InputMap::~InputMap()
{
    clear();
    delete node_index;
    delete msg_index;
    delete recovery_index;
    log_info << "inserted: " << inserted << " updated aru: " << updated_aru;
}



bool gcomm::evs::InputMap::is_safe(iterator i) const
{
    const Seqno seq(MsgIndex::get_value(i).get_msg().get_seq());
    return (safe_seq != Seqno::max() && seq <= safe_seq);
}


bool gcomm::evs::InputMap::is_agreed(iterator i) const
{
    const Seqno seq(MsgIndex::get_value(i).get_msg().get_seq());
    return (aru_seq != Seqno::max() && seq <= aru_seq);
}


bool gcomm::evs::InputMap::is_fifo(iterator i) const
{
    const Seqno seq(MsgIndex::get_value(i).get_msg().get_seq());
    const Node& node(NodeIndex::get_value(
                                 node_index->find_checked(
                                     MsgIndex::get_value(i).get_uuid())));
    return (node.get_range().get_lu() > seq);
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::begin() const
{
    return msg_index->begin();
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::end() const
{
    return msg_index->end();
}


gcomm::evs::Range gcomm::evs::InputMap::insert(
    const UUID& uuid, 
    const UserMessage& msg, 
    const ReadBuf* const rb, 
    const size_t offset)
{
    /* Only insert messages with meaningful seqno */
    gcomm_assert(msg.get_seq() != Seqno::max());
    if (msg_index->empty() == false)
    {
        gcomm_assert(
            msg.get_seq() < 
            MsgIndex::get_value(msg_index->begin()).get_msg().get_seq() 
            + Seqno::max().get()/4);
    }
    
    Node& node(NodeIndex::get_value(node_index->find_checked(uuid)));
    Range range(node.get_range());
    
    /* User should check aru_seq before inserting. This check is left 
     * also in optimized builds since violating it may cause duplicate 
     * messages */
    gcomm_assert(aru_seq == Seqno::max() || aru_seq < msg.get_seq()) 
        << "aru seq " << aru_seq << " msg seq " << msg.get_seq() 
        << " index size " << msg_index->size();
    
    /* User should check LU before inserting. This check is left 
     * also in optimized builds since violating it may cause duplicate 
     * messages */
    gcomm_assert(range.get_lu() <= msg.get_seq()) 
        << "lu " << range.get_lu() << " > "
        << msg.get_seq();
    
    if (recovery_index->find(MsgKey(node.get_index(), msg.get_seq())) !=
        recovery_index->end())
    {
        log_warn << "message " << msg << " has already been delivered, state "
                 << *this;
        return node.get_range();
    }
    
    /* Loop over message seqno range and insert messages when not 
     * already found */
    for (Seqno s = msg.get_seq(); s <= msg.get_seq() + msg.get_seq_range(); ++s)
    {
        MsgIndex::iterator msg_i = msg_index->find(
            MsgKey(node.get_index(), s));
        
        if (range.get_hs() == Seqno::max() || range.get_hs() >= s)
        {
            gcomm_assert(msg_i == msg_index->end());
        }
        
        if (msg_i == msg_index->end())
        {
            ReadBuf* ins_rb(0);
            if (s == msg.get_seq())
            {
                ins_rb = (rb != 0 ? rb->copy(offset) : 0);
            }
            (void)msg_index->insert_checked(
                make_pair(MsgKey(node.get_index(), s), 
                          Msg(uuid, msg, ins_rb)));
            ++inserted;
        }
        
        /* Update highest seen */
        if (range.get_hs() == Seqno::max() || range.get_hs() < s)
        {
            range.set_hs(s);
        }
        
        /* Update lowest unseen */
        if (range.get_lu() == s)
        {
            Seqno i(s);
            do
            {
                ++i;
            }
            while (msg_index->find(MsgKey(node.get_index(), i)) 
                   != msg_index->end());
            range.set_lu(i);
        }
    }
    

    bool do_update_aru = (aru_seq == Seqno::max() || 
                          (aru_seq + 1) < range.get_lu());
    node.set_range(range);
    if (do_update_aru == true)
    {
        update_aru();
        ++updated_aru;
    }
    return range;
}

void gcomm::evs::InputMap::erase(iterator i)
{
    const UserMessage& msg(MsgIndex::get_value(i).get_msg());
    if (msg.get_seq_range() == 0)
    {
        try
        {
            gu_trace(recovery_index->insert_checked(*i));
            gu_trace(msg_index->erase(i));
        }
        catch (...)
        {
            log_fatal << "msg: " << msg;
            throw;
        }
    }
    else
    {
        if (recovery_index->insert(*i).second == false)
        {
            log_debug << "duplicate";
        }
        gu_trace(msg_index->erase(i));
    }
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::find(
    const UUID& uuid, 
    const Seqno seq) const
{
    InputMap::iterator ret;
    gu_trace(ret = msg_index->find(
                 MsgKey(NodeIndex::get_value(
                            node_index->find_checked(uuid)).get_index(), seq)));
    return ret;
}


gcomm::evs::InputMap::iterator gcomm::evs::InputMap::recover(
    const UUID& uuid,
    const Seqno seq) const
{
    NodeIndex::const_iterator node_i;
    gu_trace(node_i = node_index->find_checked(uuid));
    size_t idx = NodeIndex::get_value(node_i).get_index();
    iterator ret;
    gu_trace(ret = recovery_index->find_checked(MsgKey(idx, seq)));
    return ret;
}


struct UpdateAruLUCmp
{
    bool operator()(const pair<const UUID, InputMap::Node>& a,
                    const pair<const UUID, InputMap::Node>& b) const
    {

        return a.second.get_range().get_lu() < b.second.get_range().get_lu();
    }
};

void gcomm::evs::InputMap::update_aru()
{
    NodeIndex::const_iterator min = 
        min_element(node_index->begin(),
                    node_index->end(), UpdateAruLUCmp());
    
    const Seqno minval = NodeIndex::get_value(min).get_range().get_lu();
    // log_info << "aru seq " << aru_seq << " next " << minval;
    if (aru_seq != Seqno::max())
    {
        /* aru_seq must not decrease */
        gcomm_assert(minval - 1 >= aru_seq);
        aru_seq = minval - 1;
    }
    else if (minval == 1)
    {
        aru_seq = 0;
    }
    
}

struct SetSafeSeqCmp
{

    bool operator()(const pair<const UUID, InputMap::Node>& a,
                    const pair<const UUID, InputMap::Node>& b) const    
    {
        if (a.second.get_safe_seq() == Seqno::max())
        {
            return true;
        }
        else if (b.second.get_safe_seq() == Seqno::max())
        {
            return false;
        }
        else
        {
            return a.second.get_safe_seq() < b.second.get_safe_seq();
        }
    }
};

void gcomm::evs::InputMap::set_safe_seq(const UUID& uuid, const Seqno seq)
{
    gcomm_assert(seq != Seqno::max());
    // @note This assertion does not necessarily hold. Some other 
    // instance may well have higher all received up to seqno 
    // than this (due to packet loss). Commented out... and left
    // for future reference.
    // gcomm_assert(aru_seq != Seqno::max() && seq <= aru_seq);
    
    // Update node safe seq. Must (at least should) be updated
    // in monotonically increasing order if node works ok.
    Node& node(NodeIndex::get_value(node_index->find_checked(uuid)));
    gcomm_assert(node.get_safe_seq() == Seqno::max() || 
                 seq >= node.get_safe_seq()) 
                     << "node.safe_seq=" << node.get_safe_seq() 
                     << " seq=" << seq;
    node.set_safe_seq(seq);
    
    // Update global safe seq which must be monotonically increasing.
    NodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), SetSafeSeqCmp());
    const Seqno minval = NodeIndex::get_value(min).get_safe_seq();
    gcomm_assert(safe_seq == Seqno::max() || minval >= safe_seq);
    safe_seq = minval;
    
    // Cleanup recovery index
    cleanup_recovery_index();
}


void gcomm::evs::InputMap::cleanup_recovery_index()
{
    assert(node_index->size() > 0);
    if (safe_seq != Seqno::max())
    {
        MsgIndex::iterator i = recovery_index->lower_bound(
            MsgKey(node_index->size() - 1, safe_seq + 1));
        for_each(recovery_index->begin(), i, release_rb);
        recovery_index->erase(recovery_index->begin(), i);
    }
}

void gcomm::evs::InputMap::insert_uuid(const UUID& uuid)
{
    gcomm_assert(msg_index->empty() == true &&
                 recovery_index->empty() == true);

    (void)node_index->insert_checked(make_pair(uuid, Node()));
    size_t n = 0;
    for (NodeIndex::iterator i = node_index->begin();
         i != node_index->end(); ++i)
    {
        NodeIndex::get_value(i).set_index(n);
        ++n;
    }
}

const gcomm::evs::Range& InputMap::get_range(const UUID& uuid) const
{
    return NodeIndex::get_value(node_index->find_checked(uuid)).get_range();
}


gcomm::evs::Seqno InputMap::get_safe_seq(const UUID& uuid) const
{
    return NodeIndex::get_value(node_index->find_checked(uuid)).get_safe_seq();
}

void gcomm::evs::InputMap::clear()
{
    if (msg_index->empty() == false)
    {
        log_warn << "discarding " << msg_index->size() << 
            " messages from message index";
    }
    for_each(msg_index->begin(), msg_index->end(), release_rb);
    msg_index->clear();
    if (recovery_index->empty() == false)
    {
        log_debug << "discarding " << recovery_index->size() 
                  << " messages from recovery index";
    }
    for_each(recovery_index->begin(), recovery_index->end(), release_rb);
    recovery_index->clear();
    node_index->clear();
    aru_seq = Seqno::max();
    safe_seq = Seqno::max();
}
