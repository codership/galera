/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "evs_node.hpp"
#include "evs_message2.hpp"

#include <ostream>


std::ostream&
gcomm::evs::operator<<(std::ostream& os, const gcomm::evs::Node& n)
{
    os << "evs::node{";
    os << "operational=" << n.operational() << ",";
    os << "suspected=" << n.suspected() << ",";
    os << "installed=" << n.installed() << ",";
    os << "fifo_seq=" << n.fifo_seq() << ",";
    if (n.join_message() != 0)
    {
        os << "join_message=\n" << *n.join_message() << ",\n";
    }
    if (n.leave_message() != 0)
    {
        os << "leave_message=\n" << *n.leave_message() << ",\n";
    }
    os << "}";
    return os;
}


gcomm::evs::Node::Node(const Node& n)
    :
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
    suspect_timeout_ (n.suspect_timeout_),
    inactive_timeout_(n.inactive_timeout_),
    tstamp_          (n.tstamp_),
    fifo_seq_        (n.fifo_seq_),
    segment_         (n.segment_)
{ }


gcomm::evs::Node::~Node()
{
    delete join_message_;
    delete leave_message_;
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


bool gcomm::evs::Node::is_suspected() const
{
    return suspected_;
}

bool gcomm::evs::Node::is_inactive() const
{
    return inactive_;
}
