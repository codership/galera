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

#include "evs_input_map2.hpp"
#include "gcomm/readbuf.hpp"
#include <gu_exception.hpp>

using namespace std;
using namespace std::rel_ops;

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
        
        const gcomm::evs::InputMapNode& aval(
            gcomm::evs::InputMapNodeIndex::get_value(a));
        const gcomm::evs::InputMapNode& bval(gcomm::evs::InputMapNodeIndex::get_value(b));
        return (aval.get_range().get_lu() < bval.get_range().get_lu());
    }
};


// Compare node index safe seqs
class NodeIndexSafeSeqCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        const gcomm::evs::InputMapNode& aval(gcomm::evs::InputMapNodeIndex::get_value(a));
        const gcomm::evs::InputMapNode& bval(gcomm::evs::InputMapNodeIndex::get_value(b));
        if (aval.get_safe_seq() == gcomm::evs::Seqno::max())
        {
            return true;
        }
        else if (bval.get_safe_seq() == gcomm::evs::Seqno::max())
        {
            return false;
        }
        else
        {
            return aval.get_safe_seq() < bval.get_safe_seq();
        }
    }
};


// Release ReadBuf object from message index element
static void release_rb(gcomm::evs::InputMapMsgIndex::value_type& vt)
{
    gcomm::ReadBuf* rb = gcomm::evs::InputMapMsgIndex::get_value(vt).get_rb();
    if (rb != 0)
    {
        rb->release();
    }
}



//////////////////////////////////////////////////////////////////////////
//
// Ostream operators
//
//////////////////////////////////////////////////////////////////////////


ostream& gcomm::evs::operator<<(ostream& os, const InputMapNode& in)
{
    return (os << "node: ("
            << "idx="      << in.get_index()    << ","
            << "range="    << in.get_range()    << ","
            << "safe_seq=" << in.get_safe_seq() << ")");
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
            << "recovery_index=" << *im.recovery_index << ","
            << "inserted="       << im.inserted        << ","
            << "updated_aru="    << im.updated_aru     << "}");
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
    inserted(0),
    updated_aru(0)
{
    // 
}


gcomm::evs::InputMap::~InputMap()
{
    clear();
    delete node_index;
    delete msg_index;
    delete recovery_index;
    log_info << "inserted: " << inserted << " updated aru: " << updated_aru;
}



//////////////////////////////////////////////////////////////////////////
//
// Public member functions
//
//////////////////////////////////////////////////////////////////////////


void gcomm::evs::InputMap::insert_uuid(const UUID& uuid)
    throw (gu::Exception)
{
    gcomm_assert(msg_index->empty() == true &&
                 recovery_index->empty() == true);
    
    gu_trace((void)node_index->insert_checked(make_pair(uuid, InputMapNode())));
    size_t n = 0;
    for (InputMapNodeIndex::iterator i = node_index->begin();
         i != node_index->end(); ++i)
    {
        InputMapNodeIndex::get_value(i).set_index(n);
        ++n;
    }
}


gcomm::evs::Range gcomm::evs::InputMap::get_range(const UUID& uuid) const
    throw (gu::Exception)
{
    return InputMapNodeIndex::get_value(
        node_index->find_checked(uuid)).get_range();
}


gcomm::evs::Seqno gcomm::evs::InputMap::get_safe_seq(const UUID& uuid) const
    throw (gu::Exception)
{
    return InputMapNodeIndex::get_value(
        node_index->find_checked(uuid)).get_safe_seq();
}


void gcomm::evs::InputMap::set_safe_seq(const UUID& uuid, const Seqno seq)
    throw (gu::Exception)
{
    gcomm_assert(seq != static_cast<const Seqno>(Seqno::max()));
    // @note This assertion does not necessarily hold. Some other 
    // instance may well have higher all received up to seqno 
    // than this (due to packet loss). Commented out... and left
    // for future reference.
    // gcomm_assert(aru_seq != Seqno::max() && seq <= aru_seq);
    
    // Update node safe seq. Must (at least should) be updated
    // in monotonically increasing order if node works ok.
    InputMapNode& node(InputMapNodeIndex::get_value(node_index->find_checked(uuid)));
    gcomm_assert(node.get_safe_seq() == Seqno::max() || 
                 seq >= node.get_safe_seq()) 
        << "node.safe_seq=" << node.get_safe_seq() 
        << " seq=" << seq;
    node.set_safe_seq(seq);
    
    // Update global safe seq which must be monotonically increasing.
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), 
                    NodeIndexSafeSeqCmpOp());
    const Seqno minval = InputMapNodeIndex::get_value(min).get_safe_seq();
    gcomm_assert(safe_seq == Seqno::max() || minval >= safe_seq);
    safe_seq = minval;

    // Global safe seq must always be smaller than equal to aru seq
    gcomm_assert(safe_seq == Seqno::max() ||
                 (aru_seq != Seqno::max() && safe_seq <= aru_seq));
    // Cleanup recovery index
    cleanup_recovery_index();
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


bool gcomm::evs::InputMap::is_safe(iterator i) const
    throw (gu::Exception)
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    return (safe_seq != Seqno::max() && seq <= safe_seq);
}


bool gcomm::evs::InputMap::is_agreed(iterator i) const
    throw (gu::Exception)
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    return (aru_seq != Seqno::max() && seq <= aru_seq);
}


bool gcomm::evs::InputMap::is_fifo(iterator i) const
    throw (gu::Exception)
{
    const Seqno seq(InputMapMsgIndex::get_value(i).get_msg().get_seq());
    const InputMapNode& node(InputMapNodeIndex::get_value(
                                 node_index->find_checked(
                                     InputMapMsgIndex::get_value(i).get_uuid())));
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


gcomm::evs::Range 
gcomm::evs::InputMap::insert(const UUID& uuid, 
                             const UserMessage& msg, 
                             const ReadBuf* const rb, 
                             const size_t offset)
    throw (gu::Exception)
{
    // Only insert messages with meaningful seqno
    gcomm_assert(msg.get_seq() != Seqno::max());
    if (msg_index->empty() == false)
    {
        gcomm_assert(
            msg.get_seq() < 
            InputMapMsgIndex::get_value(msg_index->begin()).get_msg().get_seq() 
            + Seqno::max().get()/4);
    }
    
    InputMapNode& node(InputMapNodeIndex::get_value(node_index->find_checked(uuid)));
    Range range(node.get_range());
    
    // User should check aru_seq before inserting. This check is left 
    // also in optimized builds since violating it may cause duplicate 
    // messages.
    gcomm_assert(aru_seq == Seqno::max() || aru_seq < msg.get_seq()) 
        << "aru seq " << aru_seq << " msg seq " << msg.get_seq() 
        << " index size " << msg_index->size();
    
    // User should check LU before inserting. This check is left 
    // also in optimized builds since violating it may cause duplicate 
    // messages
    gcomm_assert(range.get_lu() <= msg.get_seq()) 
        << "lu " << range.get_lu() << " > "
        << msg.get_seq();
    
    if (recovery_index->find(InputMapMsgKey(node.get_index(), msg.get_seq())) !=
        recovery_index->end())
    {
        log_warn << "message " << msg << " has already been delivered, state "
                 << *this;
        return node.get_range();
    }
    
    // Loop over message seqno range and insert messages when not 
    // already found
    for (Seqno s = msg.get_seq(); s <= msg.get_seq() + msg.get_seq_range(); ++s)
    {
        InputMapMsgIndex::iterator msg_i = msg_index->find(
            InputMapMsgKey(node.get_index(), s));
        
        if (range.get_hs() == Seqno::max() || range.get_hs() < s)
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
            gu_trace((void)msg_index->insert_checked(
                         make_pair(InputMapMsgKey(node.get_index(), s), 
                                   InputMapMsg(uuid, msg, ins_rb))));
            ++inserted;
        }
        
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
    }
    
    // Call update aru only if aru_seq may change
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
    throw (gu::Exception)
{
    const UserMessage& msg(InputMapMsgIndex::get_value(i).get_msg());
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


gcomm::evs::InputMap::iterator 
gcomm::evs::InputMap::find(const UUID& uuid, const Seqno seq) const
    throw (gu::Exception)
{
    iterator ret;
    InputMapNodeIndex::const_iterator node_i;
    gu_trace(node_i = node_index->find_checked(uuid));
    const InputMapNode& node(InputMapNodeIndex::get_value(node_i));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = msg_index->find(key));
    return ret;
}


gcomm::evs::InputMap::iterator 
gcomm::evs::InputMap::recover(const UUID& uuid, const Seqno seq) const
    throw (gu::Exception)
{
    iterator ret;
    InputMapNodeIndex::const_iterator node_i;
    gu_trace(node_i = node_index->find_checked(uuid));
    const InputMapNode& node(InputMapNodeIndex::get_value(node_i));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = recovery_index->find_checked(key));
    return ret;
}



//////////////////////////////////////////////////////////////////////////
//
// Private member functions
//
//////////////////////////////////////////////////////////////////////////


void gcomm::evs::InputMap::update_aru()
    throw (gu::Exception)
{
    InputMapNodeIndex::const_iterator min = 
        min_element(node_index->begin(), node_index->end(), NodeIndexLUCmpOp());
    
    const Seqno minval = InputMapNodeIndex::get_value(min).get_range().get_lu();
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
            InputMapMsgKey(node_index->size() - 1, safe_seq + 1));
        for_each(recovery_index->begin(), i, release_rb);
        recovery_index->erase(recovery_index->begin(), i);
    }
}


