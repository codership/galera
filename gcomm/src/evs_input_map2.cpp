/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*! 
 * @file evs_input_map2.cpp
 *
 * Input map implementation
 *
 * @author Teemu Ollakka <teemu.ollakka@codership.com>
 */

#ifdef PROFILE_EVS_INPUT_MAP
// #define GCOMM_PROFILE 1
#endif // PROFILE_EVS_INPUT_MAP

#include "evs_input_map2.hpp"
#include "gu_exception.hpp"
#include <stdexcept>
#include <numeric>

using namespace std;
using namespace std::rel_ops;

using namespace gu::net;

//////////////////////////////////////////////////////////////////////////
//
// Static operators and functions
//
//////////////////////////////////////////////////////////////////////////


// Compare node index LUs
class NodeIndexLUCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        
        return (a.get_range().get_lu() < b.get_range().get_lu());
    }
};

class NodeIndexHSCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        
        if (a.get_range().get_hs() == gcomm::evs::Seqno::max())
        {
            return true;
        }
        else if (b.get_range().get_hs() == gcomm::evs::Seqno::max())
        {
            return false;
        }
        else
        {
            return (a.get_range().get_hs() < b.get_range().get_hs());
        }
    }
};


// Compare node index safe seqs
class NodeIndexSafeSeqCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        if (a.get_safe_seq() == gcomm::evs::Seqno::max())
        {
            return true;
        }
        else if (b.get_safe_seq() == gcomm::evs::Seqno::max())
        {
            return false;
        }
        else
        {
            return a.get_safe_seq() < b.get_safe_seq();
        }
    }
};





//////////////////////////////////////////////////////////////////////////
//
// Ostream operators
//
//////////////////////////////////////////////////////////////////////////


ostream& gcomm::evs::operator<<(ostream& os, const InputMapNode& in)
{
    return (os << "node: {"
            << "idx="      << in.get_index()    << ","
            << "range="    << in.get_range()    << ","
            << "safe_seq=" << in.get_safe_seq() << "}");
}

ostream& gcomm::evs::operator<<(ostream& os, const InputMapNodeIndex& ni)
{
    copy(ni.begin(), ni.end(), ostream_iterator<const InputMapNode>(os, " "));
    return os;
}

ostream& gcomm::operator<<(ostream& os, const InputMapMsgKey& mk)
{
    return (os << "(" << mk.get_index() << "," << mk.get_seq() << ")");
}


ostream& gcomm::evs::operator<<(ostream& os, const InputMapMsg& m)
{
    return os;
}


ostream& gcomm::evs::operator<<(ostream& os, const InputMap& im)
{
    return (os << "evs::input_map: {"
            << "aru_seq="        << im.get_aru_seq()   << ","
            << "safe_seq="       << im.get_safe_seq()  << ","
            << "node_index="     << *im.node_index     << ","
            << "msg_index="      << *im.msg_index      << ","
            << "recovery_index=" << *im.recovery_index << "}");
}



//////////////////////////////////////////////////////////////////////////
//
// Constructors/destructors
//
//////////////////////////////////////////////////////////////////////////


gcomm::evs::InputMap::InputMap() :
    safe_seq(Seqno::max()),
    aru_seq(Seqno::max()),
    node_index(new InputMapNodeIndex()),
    msg_index(new InputMapMsgIndex()),
    recovery_index(new InputMapMsgIndex()),
    n_msgs(O_SAFE + 1),
    max_droppable(16),
    prof("input_map_intrnl")
{
    // 
}


gcomm::evs::InputMap::~InputMap()
{
    clear();
    delete node_index;
    delete msg_index;
    delete recovery_index;
#ifdef GCOMM_PROFILE
    log_info << "profile: " << prof;
#endif // GCOMM_PROFILE
}



//////////////////////////////////////////////////////////////////////////
//
// Public member functions
//
//////////////////////////////////////////////////////////////////////////


void gcomm::evs::InputMap::reset(const size_t nodes, const size_t window)
    throw (gu::Exception)
{
    gcomm_assert(msg_index->empty() == true &&
                 recovery_index->empty() == true &&
                 accumulate(n_msgs.begin(), n_msgs.end(), 0) == 0);
    node_index->clear();
    
    log_info << " size " << node_index->size();
    gu_trace(node_index->resize(nodes, InputMapNode()));
    for (size_t i = 0; i < nodes; ++i)
    {
        node_index->at(i).set_index(i);
    }
    log_info << *node_index << " size " << node_index->size();
}





gcomm::evs::Seqno gcomm::evs::InputMap::get_min_hs() const
    throw (gu::Exception)
{
    Seqno ret;
    gcomm_assert(node_index->empty() == false);
    ret = min_element(node_index->begin(),
                      node_index->end(), 
                      NodeIndexHSCmpOp())->get_range().get_hs();
    return ret;
}


gcomm::evs::Seqno gcomm::evs::InputMap::get_max_hs() const
    throw (gu::Exception)
{
    Seqno ret;
    gcomm_assert(node_index->empty() == false);
    ret = max_element(node_index->begin(),
                      node_index->end(),
                      NodeIndexHSCmpOp())->get_range().get_hs();
    return ret;
}


void gcomm::evs::InputMap::set_safe_seq(const size_t uuid, const Seqno seq)
    throw (gu::Exception)
{
    profile_enter(prof);
    gcomm_assert(seq != static_cast<const Seqno>(Seqno::max()));
    // @note This assertion does not necessarily hold. Some other 
    // instance may well have higher all received up to seqno 
    // than this (due to packet loss). Commented out... and left
    // for future reference.
    // gcomm_assert(aru_seq != Seqno::max() && seq <= aru_seq);
    
    // Update node safe seq. Must (at least should) be updated
    // in monotonically increasing order if node works ok.
    InputMapNode& node(node_index->at(uuid));
    gcomm_assert(node.get_safe_seq() == Seqno::max() || 
                 seq >= node.get_safe_seq()) 
        << "node.safe_seq=" << node.get_safe_seq() 
        << " seq=" << seq;
    node.set_safe_seq(seq);
    
    // Update global safe seq which must be monotonically increasing.
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), 
                    NodeIndexSafeSeqCmpOp());
    const Seqno minval = min->get_safe_seq();
    gcomm_assert(safe_seq == Seqno::max() || minval >= safe_seq);
    safe_seq = minval;
    
    // Global safe seq must always be smaller than equal to aru seq
    gcomm_assert(safe_seq == Seqno::max() ||
                 (aru_seq != Seqno::max() && safe_seq <= aru_seq));
    // Cleanup recovery index
    cleanup_recovery_index();
    profile_leave(prof);
}


void gcomm::evs::InputMap::clear()
{
    profile_enter(prof);
    if (msg_index->empty() == false)
    {
        log_warn << "discarding " << msg_index->size() << 
            " messages from message index";
    }
    msg_index->clear();
    if (recovery_index->empty() == false)
    {
        log_debug << "discarding " << recovery_index->size() 
                  << " messages from recovery index";
    }
    recovery_index->clear();
    node_index->clear();
    aru_seq = Seqno::max();
    safe_seq = Seqno::max();
    fill(n_msgs.begin(), n_msgs.end(), 0);
    profile_leave(prof);
}


gcomm::evs::Range 
gcomm::evs::InputMap::insert(const size_t uuid, 
                             const UserMessage& msg, 
                             const Datagram& rb)
    throw (gu::Exception)
{
    try
    {
    assert(rb.is_normalized() == true);
    Range range;
        
    profile_enter(prof);

    // Only insert messages with meaningful seqno
    gcomm_assert(msg.get_seq() != Seqno::max());
    if (msg_index->empty() == false)
    {
        gcomm_assert(
            msg.get_seq() < 
            InputMapMsgIndex::get_value(msg_index->begin()).get_msg().get_seq() 
            + Seqno::max().get()/4);
    }
    
    // User should check aru_seq before inserting. This check is left 
    // also in optimized builds since violating it may cause duplicate 
    // messages.
    gcomm_assert(aru_seq == Seqno::max() || aru_seq < msg.get_seq())
        << "aru seq " << aru_seq << " msg seq " << msg.get_seq() 
        << " index size " << msg_index->size();
    
    profile_leave(prof);
    
    
    InputMapNode& node(node_index->at(uuid));
    range = node.get_range();
    
    
    profile_enter(prof);
    
    // User should check LU before inserting. This check is left 
    // also in optimized builds since violating it may cause duplicate 
    // messages
    gcomm_assert(range.get_lu() <= msg.get_seq())
        << "lu " << range.get_lu() << " > "
        << msg.get_seq();
    
    if (msg.get_seq() < node.get_range().get_lu() ||
        (node.get_range().get_hs() != Seqno::max() &&
         msg.get_seq() <= node.get_range().get_hs() &&
         recovery_index->find(InputMapMsgKey(node.get_index(), msg.get_seq())) !=
         recovery_index->end()))
    {
        // log_warn << "message " 
        // << msg << " has already been delivered, state "
        // << *this;
        return node.get_range();
    }
    profile_leave(prof);
    
    // Loop over message seqno range and insert messages when not 
    // already found

    for (Seqno s = msg.get_seq(); s <= msg.get_seq() + msg.get_seq_range(); ++s)
    {
        InputMapMsgIndex::iterator msg_i;

        profile_enter(prof);        
        if (range.get_hs() == Seqno::max() || range.get_hs() < s)
        {
            msg_i = msg_index->end();
        }
        else
        {
            msg_i = msg_index->find(InputMapMsgKey(node.get_index(), s));
        }
        
        if (msg_i == msg_index->end())
        {
            gu_trace((void)msg_index->insert_unique(
                         make_pair(InputMapMsgKey(node.get_index(), s), 
                                   InputMapMsg(msg, 
                                               s == msg.get_seq() ? rb : 
                                               Datagram()))));
            ++n_msgs[msg.get_order()];
        }
        profile_leave(prof); 
        profile_enter(prof);
        // Update highest seen
        if (range.get_hs() == Seqno::max() || range.get_hs() < s)
        {
            range.set_hs(s);
        }
        
        // Update lowest unseen
        if (range.get_lu() == s)
        {
            Seqno i(s);
            do
            {
                ++i;
            }
            while (
                i <= range.get_hs() &&
                (msg_index->find(InputMapMsgKey(node.get_index(), i)) 
                 != msg_index->end() ||
                 recovery_index->find(InputMapMsgKey(node.get_index(), i)) 
                 != recovery_index->end()));
            range.set_lu(i);
        }
        profile_leave(prof);        
    }

    // Call update aru only if aru_seq may change
    bool do_update_aru = (aru_seq == Seqno::max() || 
                          (aru_seq + 1) < range.get_lu());
    node.set_range(range);
    if (do_update_aru == true)
    {
        profile_enter(prof);
        update_aru();
        profile_leave(prof);
    }
    return range;
    }
    catch (...)
    {
        gu_throw_fatal;
        throw;
    }
}


void gcomm::evs::InputMap::erase(iterator i)
    throw (gu::Exception)
{
    const UserMessage& msg(InputMapMsgIndex::get_value(i).get_msg());
    profile_enter(prof);
    --n_msgs[msg.get_order()];
    gu_trace(recovery_index->insert_unique(*i));
    gu_trace(msg_index->erase(i));
    profile_leave(prof);
}


gcomm::evs::InputMap::iterator 
gcomm::evs::InputMap::find(const size_t uuid, const Seqno seq) const
    throw (gu::Exception)
{
    iterator ret;
    profile_enter(prof);
    const InputMapNode& node(node_index->at(uuid));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = msg_index->find(key));
    profile_leave(prof);
    return ret;
}


gcomm::evs::InputMap::iterator 
gcomm::evs::InputMap::recover(const size_t uuid, const Seqno seq) const
    throw (gu::Exception)
{
    iterator ret;
    profile_enter(prof);
    const InputMapNode& node(node_index->at(uuid));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = recovery_index->find_checked(key));
    profile_leave(prof);
    return ret;
}



//////////////////////////////////////////////////////////////////////////
//
// Private member functions
//
//////////////////////////////////////////////////////////////////////////


inline void gcomm::evs::InputMap::update_aru()
    throw (gu::Exception)
{
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), NodeIndexLUCmpOp());
    
    const Seqno minval = min->get_range().get_lu();
    gcomm_assert(minval != Seqno::max());
    // log_debug << "aru seq " << aru_seq << " next " << minval;
    if (aru_seq != Seqno::max() ||
        (aru_seq == Seqno::max() && minval > Seqno(0)))
    {
        /* aru_seq must not decrease */
        gcomm_assert(aru_seq == Seqno::max() || minval - 1 >= aru_seq);
        aru_seq = minval - 1;
    }
}


void gcomm::evs::InputMap::cleanup_recovery_index()
    throw (gu::Exception)
{
    gcomm_assert(node_index->size() > 0);
    if (safe_seq != Seqno::max())
    {
        InputMapMsgIndex::iterator i = recovery_index->lower_bound(
            InputMapMsgKey(0, safe_seq + 1));
        recovery_index->erase(recovery_index->begin(), i);
    }
}


