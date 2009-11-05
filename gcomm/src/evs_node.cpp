/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#include "evs_node.hpp"
#include "evs_message2.hpp"

#include <ostream>

using namespace std;
using namespace gu::datetime;
using namespace gcomm::evs;

ostream& gcomm::evs::operator<<(ostream& os, const Node& n)
{
    os << "evs::node{";
    os << "operational=" << n.get_operational() << ",";
    os << "installed=" << n.get_installed() << ",";
    os << "fifo_seq=" << n.get_fifo_seq() << ",";
    if (n.get_join_message() != 0)
    {
        os << "join_message=" << *n.get_join_message() << ",";
    }
    if (n.get_leave_message() != 0)
    {
        os << "leave_message=" << *n.get_leave_message() << ",";
    }
    os << "}";
    return os;
}


gcomm::evs::Node::Node(const Node& n) :
    index(n.index),
    operational(n.operational),
    installed(n.installed),
    join_message(n.join_message != 0 ? new JoinMessage(*n.join_message) : 0),
    leave_message(n.leave_message != 0 ? new LeaveMessage(*n.leave_message) : 0),
    inactive_timeout(n.inactive_timeout),
    tstamp(n.tstamp),
    fifo_seq(n.fifo_seq)
{ }


gcomm::evs::Node::~Node()
{
    delete join_message;
    delete leave_message;
}


void gcomm::evs::Node::set_join_message(const JoinMessage* jm)
{
    if (join_message != 0)
    {
        delete join_message;
    }
    if (jm != 0)
    {
        join_message = new JoinMessage(*jm);
    }
    else
    {
        join_message = 0;
    }
}


void gcomm::evs::Node::set_leave_message(const LeaveMessage* lm)
{
    if (leave_message != 0)
    {
        delete leave_message;
    }
    if (lm != 0)
    {
        leave_message = new LeaveMessage(*lm);
    }
    else
    {
        leave_message = 0;
    }
}


bool gcomm::evs::Node::is_inactive() const
{
    return (get_tstamp() + inactive_timeout < Date::now());
}
