/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#include "evs_node.hpp"
#include "evs_proto.hpp"
#include "evs_message2.hpp"

#include <ostream>

const size_t gcomm::evs::Node::invalid_index(std::numeric_limits<size_t>::max());

std::ostream&
gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::Node& n)
{
    os << "{";
    os << "o=" << n.operational() << ",";
    os << "s=" << n.suspected() << ",";
    os << "i=" << n.installed() << ",";
    os << "fs=" << n.fifo_seq() << ",";
    if (n.join_message() != 0)
    {
        os << "jm=\n" << *n.join_message() << ",\n";
    }
    if (n.leave_message() != 0)
    {
        os << "lm=\n" << *n.leave_message() << ",\n";
    }
    os << "}";
    return os;
}


gcomm::evs::Node::Node(const Node& n)
    :
    proto_           (n.proto_),
    index_           (n.index_),
    operational_     (n.operational_),
    suspected_       (n.suspected_),
    inactive_        (n.inactive_),
    committed_       (n.committed_),
    installed_       (n.installed_),
    join_message_    (n.join_message_ != 0 ?
                      new JoinMessage(*n.join_message_) : 0),
    leave_message_   (n.leave_message_ != 0 ?
                      new LeaveMessage(*n.leave_message_) : 0),
    delayed_list_message_ (n.delayed_list_message_ != 0 ?
                         new DelayedListMessage(*n.delayed_list_message_) : 0),
    tstamp_          (n.tstamp_),
    seen_tstamp_     (n.seen_tstamp_),
    last_requested_range_tstamp_(),
    last_requested_range_(),
    fifo_seq_        (n.fifo_seq_),
    segment_         (n.segment_)
{ }


gcomm::evs::Node::~Node()
{
    delete join_message_;
    delete leave_message_;
    delete delayed_list_message_;
}


void gcomm::evs::Node::set_join_message(const JoinMessage* jm)
{
    if (join_message_ != 0)
    {
        delete join_message_;
    }
    if (jm != 0)
    {
        join_message_ = new JoinMessage(*jm);
    }
    else
    {
        join_message_ = 0;
    }
}


void gcomm::evs::Node::set_leave_message(const LeaveMessage* lm)
{
    if (leave_message_ != 0)
    {
        delete leave_message_;
    }
    if (lm != 0)
    {
        leave_message_ = new LeaveMessage(*lm);
    }
    else
    {
        leave_message_ = 0;
    }
}

void gcomm::evs::Node::set_delayed_list_message(const DelayedListMessage* elm)
{
    if (delayed_list_message_ != 0)
    {
        delete delayed_list_message_;
    }
    delayed_list_message_ = (elm == 0 ? 0 : new DelayedListMessage(*elm));
}

bool gcomm::evs::Node::is_suspected() const
{
    return suspected_;
}

bool gcomm::evs::Node::is_inactive() const
{
    return inactive_;
}


void gcomm::evs::InspectNode::operator()(std::pair<const gcomm::UUID, Node>& p) const
{
    Node& node(p.second);
    gu::datetime::Date now(gu::datetime::Date::monotonic());
    if (node.tstamp() + node.proto_.suspect_timeout_ < now)
    {
        if (node.suspected_ == false)
        {
            log_debug << "declaring node with index "
                      << node.index_
                      << " suspected, timeout "
                      << node.proto_.suspect_timeout_;
        }
        node.suspected_ = true;
    }
    else
    {
        node.suspected_ = false;
    }
    if (node.tstamp() + node.proto_.inactive_timeout_ < now)
    {
        if (node.inactive_ == false)
        {
            log_debug << "declaring node with index "
                      << node.index_ << " inactive ";
        }
        node.inactive_ = true;
    }
    else
    {
        node.inactive_ = false;
    }
}
