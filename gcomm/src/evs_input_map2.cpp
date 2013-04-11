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

        return (a.range().lu() < b.range().lu());
    }
};

class NodeIndexHSCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        return (a.range().hs() < b.range().hs());
    }
};


// Compare node index safe seqs
class NodeIndexSafeSeqCmpOp
{
public:
    bool operator()(const gcomm::evs::InputMapNodeIndex::value_type& a,
                    const gcomm::evs::InputMapNodeIndex::value_type& b) const
    {
        return a.safe_seq() < b.safe_seq();
    }
};





//////////////////////////////////////////////////////////////////////////
//
// Ostream operators
//
//////////////////////////////////////////////////////////////////////////


std::ostream& gcomm::evs::operator<<(std::ostream& os, const InputMapNode& in)
{
    return (os << "node: {"
            << "idx="      << in.index()    << ","
            << "range="    << in.range()    << ","
            << "safe_seq=" << in.safe_seq() << "}");
}

std::ostream& gcomm::evs::operator<<(std::ostream& os, const InputMapNodeIndex& ni)
{
    copy(ni.begin(), ni.end(), std::ostream_iterator<const InputMapNode>(os, " "));
    return os;
}

std::ostream& gcomm::operator<<(std::ostream& os, const InputMapMsgKey& mk)
{
    return (os << "(" << mk.index() << "," << mk.seq() << ")");
}


std::ostream& gcomm::evs::operator<<(std::ostream& os, const InputMapMsg& m)
{
    return (os << m.msg());
}


std::ostream& gcomm::evs::operator<<(std::ostream& os, const InputMap& im)
{
    return (os << "evs::input_map: {"
            << "aru_seq="        << im.aru_seq()   << ","
            << "safe_seq="       << im.safe_seq()  << ","
            << "node_index="     << *im.node_index_
#ifndef NDEBUG
            << ","
            << "msg_index="      << *im.msg_index_      << ","
            << "recovery_index=" << *im.recovery_index_
#endif // !NDEBUG
            << "}");
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


gcomm::evs::seqno_t gcomm::evs::InputMap::min_hs() const
{
    seqno_t ret;
    gcomm_assert(node_index_->empty() == false);
    ret = min_element(node_index_->begin(),
                      node_index_->end(),
                      NodeIndexHSCmpOp())->range().hs();
    return ret;
}


gcomm::evs::seqno_t gcomm::evs::InputMap::max_hs() const
{
    seqno_t ret;
    gcomm_assert(node_index_->empty() == false);
    ret = max_element(node_index_->begin(),
                      node_index_->end(),
                      NodeIndexHSCmpOp())->range().hs();
    return ret;
}


void gcomm::evs::InputMap::set_safe_seq(const size_t uuid, const seqno_t seq)
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
    gcomm_assert(seq >= node.safe_seq())
        << "node.safe_seq=" << node.safe_seq()
        << " seq=" << seq;
    node.set_safe_seq(seq);

    // Update global safe seq which must be monotonically increasing.
    InputMapNodeIndex::const_iterator min =
        min_element(node_index_->begin(), node_index_->end(),
                    NodeIndexSafeSeqCmpOp());
    const seqno_t minval = min->safe_seq();
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
{
    Range range;

    // Only insert messages with meaningful seqno
    gcomm_assert(msg.seq() > -1);

    // User should check aru_seq before inserting. This check is left
    // also in optimized builds since violating it may cause duplicate
    // messages.
    gcomm_assert(aru_seq_ < msg.seq())
        << "aru seq " << aru_seq_ << " msg seq " << msg.seq()
        << " index size " << msg_index_->size();

    gcomm_assert(uuid < node_index_->size());
    InputMapNode& node((*node_index_)[uuid]);
    range = node.range();

    // User should check LU before inserting. This check is left
    // also in optimized builds since violating it may cause duplicate
    // messages
    gcomm_assert(range.lu() <= msg.seq())
        << "lu " << range.lu() << " > "
        << msg.seq();

    // Check whether this message has already been seen
    if (msg.seq() < node.range().lu() ||
        (msg.seq() <= node.range().hs() &&
         recovery_index_->find(InputMapMsgKey(node.index(), msg.seq())) !=
         recovery_index_->end()))
    {
        return node.range();
    }

    // Loop over message seqno range and insert messages when not
    // already found
    for (seqno_t s = msg.seq(); s <= msg.seq() + msg.seq_range(); ++s)
    {
        InputMapMsgIndex::iterator msg_i;

        if (range.hs() < s)
        {
            msg_i = msg_index_->end();
        }
        else
        {
            msg_i = msg_index_->find(InputMapMsgKey(node.index(), s));
        }

        if (msg_i == msg_index_->end())
        {
            Datagram ins_dg(s == msg.seq() ?
                                Datagram(rb)   :
                                Datagram());
            gu_trace((void)msg_index_->insert_unique(
                         std::make_pair(
                             InputMapMsgKey(node.index(), s),
                             InputMapMsg(
                                 (s == msg.seq() ?
                                  msg :
                                  UserMessage(msg.version(),
                                              msg.source(),
                                              msg.source_view_id(),
                                              s,
                                              msg.aru_seq(),
                                              0,
                                              O_DROP)), ins_dg))));
            ++n_msgs_[msg.order()];
        }

        // Update highest seen
        if (range.hs() < s)
        {
            range.set_hs(s);
        }

        // Update lowest unseen
        if (range.lu() == s)
        {
            seqno_t i(s);
            do
            {
                ++i;
            }
            while (
                i <= range.hs() &&
                (msg_index_->find(InputMapMsgKey(node.index(), i))
                 != msg_index_->end() ||
                 recovery_index_->find(InputMapMsgKey(node.index(), i))
                 != recovery_index_->end()));
            range.set_lu(i);
        }
    }

    node.set_range(range);
    update_aru();
    return range;
}


void gcomm::evs::InputMap::erase(iterator i)
{
    const UserMessage& msg(InputMapMsgIndex::value(i).msg());
    --n_msgs_[msg.order()];
    gu_trace(recovery_index_->insert_unique(*i));
    gu_trace(msg_index_->erase(i));
}


gcomm::evs::InputMap::iterator
gcomm::evs::InputMap::find(const size_t uuid, const seqno_t seq) const
{
    iterator ret;
    const InputMapNode& node(node_index_->at(uuid));
    const InputMapMsgKey key(node.index(), seq);
    gu_trace(ret = msg_index_->find(key));
    return ret;
}


gcomm::evs::InputMap::iterator
gcomm::evs::InputMap::recover(const size_t uuid, const seqno_t seq) const
{
    iterator ret;
    const InputMapNode& node(node_index_->at(uuid));
    const InputMapMsgKey key(node.index(), seq);
    gu_trace(ret = recovery_index_->find_checked(key));
    return ret;
}



//////////////////////////////////////////////////////////////////////////
//
// Private member functions
//
//////////////////////////////////////////////////////////////////////////


inline void gcomm::evs::InputMap::update_aru()
{
    InputMapNodeIndex::const_iterator min =
        min_element(node_index_->begin(), node_index_->end(), NodeIndexLUCmpOp());

    const seqno_t minval = min->range().lu();
    /* aru_seq must not decrease */
    gcomm_assert(minval - 1 >= aru_seq_);
    aru_seq_ = minval - 1;
}


void gcomm::evs::InputMap::cleanup_recovery_index()
{
    gcomm_assert(node_index_->size() > 0);
    InputMapMsgIndex::iterator i = recovery_index_->lower_bound(
        InputMapMsgKey(0, safe_seq_ + 1));
    recovery_index_->erase(recovery_index_->begin(), i);
}
