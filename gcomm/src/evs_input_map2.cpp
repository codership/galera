/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */


#include "evs_input_map2.hpp"
#include "gcomm/util.hpp"
#include "gu_exception.hpp"
#include "gu_logger.hpp"
#include "gu_buffer.hpp"
#include <stdexcept>
#include <numeric>

using namespace gu;
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
        return (a.get_range().get_hs() < b.get_range().get_hs());
    }
};


// Compare node index safe seqs
class NodeIndexSafeSeqCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        return a.get_safe_seq() < b.get_safe_seq();
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
    return (os << m.get_msg());
}


ostream& gcomm::evs::operator<<(ostream& os, const InputMap& im)
{
    return (os << "evs::input_map: {"
            << "aru_seq="        << im.get_aru_seq()   << ","
            << "safe_seq="       << im.get_safe_seq()  << ","
            << "node_index="     << *im.node_index_     << ","
            << "msg_index="      << *im.msg_index_      << ","
            << "recovery_index=" << *im.recovery_index_ << "}");
}



//////////////////////////////////////////////////////////////////////////
//
// Constructors/destructors
//
//////////////////////////////////////////////////////////////////////////


gcomm::evs::InputMap::InputMap() :
    window_         (-1),
    safe_seq_       (-1),
    aru_seq_        (-1),
    node_index_     (new InputMapNodeIndex()),
    msg_index_      (new InputMapMsgIndex()),
    recovery_index_ (new InputMapMsgIndex()),
    n_msgs_         (O_SAFE + 1),
    max_droppable_  (16)
{ }


gcomm::evs::InputMap::~InputMap()
{
    clear();
    delete node_index_;
    delete msg_index_;
    delete recovery_index_;
}



//////////////////////////////////////////////////////////////////////////
//
// Public member functions
//
//////////////////////////////////////////////////////////////////////////


void gcomm::evs::InputMap::reset(const size_t nodes, const seqno_t window)
    throw (gu::Exception)
{
    gcomm_assert(msg_index_->empty()                           == true &&
                 recovery_index_->empty()                      == true &&
                 accumulate(n_msgs_.begin(), n_msgs_.end(), 0) == 0);
    node_index_->clear();

    window_ = window;
    log_debug << " size " << node_index_->size();
    gu_trace(node_index_->resize(nodes, InputMapNode()));
    for (size_t i = 0; i < nodes; ++i)
    {
        node_index_->at(i).set_index(i);
    }
    log_debug << *node_index_ << " size " << node_index_->size();
}





gcomm::evs::seqno_t gcomm::evs::InputMap::get_min_hs() const
    throw (gu::Exception)
{
    seqno_t ret;
    gcomm_assert(node_index_->empty() == false);
    ret = min_element(node_index_->begin(),
                      node_index_->end(),
                      NodeIndexHSCmpOp())->get_range().get_hs();
    return ret;
}


gcomm::evs::seqno_t gcomm::evs::InputMap::get_max_hs() const
    throw (gu::Exception)
{
    seqno_t ret;
    gcomm_assert(node_index_->empty() == false);
    ret = max_element(node_index_->begin(),
                      node_index_->end(),
                      NodeIndexHSCmpOp())->get_range().get_hs();
    return ret;
}


void gcomm::evs::InputMap::set_safe_seq(const size_t uuid, const seqno_t seq)
    throw (gu::Exception)
{
    gcomm_assert(seq != -1);
    // @note This assertion does not necessarily hold. Some other
    // instance may well have higher all received up to seqno
    // than this (due to packet loss). Commented out... and left
    // for future reference.
    // gcomm_assert(aru_seq != seqno_t::max() && seq <= aru_seq);

    // Update node safe seq. Must (at least should) be updated
    // in monotonically increasing order if node works ok.
    InputMapNode& node(node_index_->at(uuid));
    gcomm_assert(seq >= node.get_safe_seq())
        << "node.safe_seq=" << node.get_safe_seq()
        << " seq=" << seq;
    node.set_safe_seq(seq);

    // Update global safe seq which must be monotonically increasing.
    InputMapNodeIndex::const_iterator min =
        min_element(node_index_->begin(), node_index_->end(),
                    NodeIndexSafeSeqCmpOp());
    const seqno_t minval = min->get_safe_seq();
    gcomm_assert(minval >= safe_seq_);
    safe_seq_ = minval;

    // Global safe seq must always be smaller than equal to aru seq
    gcomm_assert(safe_seq_ <= aru_seq_);
    // Cleanup recovery index
    cleanup_recovery_index();
}


void gcomm::evs::InputMap::clear()
{
    if (msg_index_->empty() == false)
    {
        log_warn << "discarding " << msg_index_->size() <<
            " messages from message index";
    }
    msg_index_->clear();
    if (recovery_index_->empty() == false)
    {
        log_debug << "discarding " << recovery_index_->size()
                  << " messages from recovery index";
    }
    recovery_index_->clear();
    node_index_->clear();
    aru_seq_ = -1;
    safe_seq_ = -1;
    fill(n_msgs_.begin(), n_msgs_.end(), 0);
}


gcomm::evs::Range
gcomm::evs::InputMap::insert(const size_t uuid,
                             const UserMessage& msg,
                             const Datagram& rb)
    throw (gu::Exception)
{
    Range range;

    // Only insert messages with meaningful seqno
    gcomm_assert(msg.get_seq() > -1);

    // User should check aru_seq before inserting. This check is left
    // also in optimized builds since violating it may cause duplicate
    // messages.
    gcomm_assert(aru_seq_ < msg.get_seq())
        << "aru seq " << aru_seq_ << " msg seq " << msg.get_seq()
        << " index size " << msg_index_->size();

    gcomm_assert(uuid < node_index_->size());
    InputMapNode& node((*node_index_)[uuid]);
    range = node.get_range();

    // User should check LU before inserting. This check is left
    // also in optimized builds since violating it may cause duplicate
    // messages
    gcomm_assert(range.get_lu() <= msg.get_seq())
        << "lu " << range.get_lu() << " > "
        << msg.get_seq();

    // Check whether this message has already been seen
    if (msg.get_seq() < node.get_range().get_lu() ||
        (msg.get_seq() <= node.get_range().get_hs() &&
         recovery_index_->find(InputMapMsgKey(node.get_index(), msg.get_seq())) !=
         recovery_index_->end()))
    {
        return node.get_range();
    }

    // Loop over message seqno range and insert messages when not
    // already found
    for (seqno_t s = msg.get_seq(); s <= msg.get_seq() + msg.get_seq_range(); ++s)
    {
        InputMapMsgIndex::iterator msg_i;

        if (range.get_hs() < s)
        {
            msg_i = msg_index_->end();
        }
        else
        {
            msg_i = msg_index_->find(InputMapMsgKey(node.get_index(), s));
        }

        if (msg_i == msg_index_->end())
        {
            Datagram ins_dg(s == msg.get_seq() ?
                            Datagram(rb)       :
                            Datagram());
            gu_trace((void)msg_index_->insert_unique(
                         make_pair(InputMapMsgKey(node.get_index(), s),
                                   InputMapMsg(
                                       (s == msg.get_seq() ?
                                        msg :
                                        UserMessage(msg.get_version(),
                                                    msg.get_source(),
                                                    msg.get_source_view_id(),
                                                    s,
                                                    msg.get_aru_seq(),
                                                    0,
                                                    O_DROP)), ins_dg))));
            ++n_msgs_[msg.get_order()];
        }

        // Update highest seen
        if (range.get_hs() < s)
        {
            range.set_hs(s);
        }

        // Update lowest unseen
        if (range.get_lu() == s)
        {
            seqno_t i(s);
            do
            {
                ++i;
            }
            while (
                i <= range.get_hs() &&
                (msg_index_->find(InputMapMsgKey(node.get_index(), i))
                 != msg_index_->end() ||
                 recovery_index_->find(InputMapMsgKey(node.get_index(), i))
                 != recovery_index_->end()));
            range.set_lu(i);
        }
    }

    node.set_range(range);
    update_aru();
    return range;
}


void gcomm::evs::InputMap::erase(iterator i)
    throw (gu::Exception)
{
    const UserMessage& msg(InputMapMsgIndex::get_value(i).get_msg());
    --n_msgs_[msg.get_order()];
    gu_trace(recovery_index_->insert_unique(*i));
    gu_trace(msg_index_->erase(i));
}


gcomm::evs::InputMap::iterator
gcomm::evs::InputMap::find(const size_t uuid, const seqno_t seq) const
    throw (gu::Exception)
{
    iterator ret;
    const InputMapNode& node(node_index_->at(uuid));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = msg_index_->find(key));
    return ret;
}


gcomm::evs::InputMap::iterator
gcomm::evs::InputMap::recover(const size_t uuid, const seqno_t seq) const
    throw (gu::Exception)
{
    iterator ret;
    const InputMapNode& node(node_index_->at(uuid));
    const InputMapMsgKey key(node.get_index(), seq);
    gu_trace(ret = recovery_index_->find_checked(key));
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
        min_element(node_index_->begin(), node_index_->end(), NodeIndexLUCmpOp());

    const seqno_t minval = min->get_range().get_lu();
    /* aru_seq must not decrease */
    gcomm_assert(minval - 1 >= aru_seq_);
    aru_seq_ = minval - 1;
}


void gcomm::evs::InputMap::cleanup_recovery_index()
    throw (gu::Exception)
{
    gcomm_assert(node_index_->size() > 0);
    InputMapMsgIndex::iterator i = recovery_index_->lower_bound(
        InputMapMsgKey(0, safe_seq_ + 1));
    recovery_index_->erase(recovery_index_->begin(), i);
}
