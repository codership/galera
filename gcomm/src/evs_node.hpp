/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef EVS_NODE_HPP
#define EVS_NODE_HPP

#include "evs_message2.hpp"

#include "gcomm/map.hpp"
#include "gcomm/uuid.hpp"
#include "gu_datetime.hpp"

#include <limits>

#include <stdint.h>



namespace gcomm
{
    namespace evs
    {
        class Node;
        class NodeMap;
        std::ostream& operator<<(std::ostream&, const Node&);
        class OperationalSelect;
    }
}


class gcomm::evs::Node
{
public:    
    Node(const gu::datetime::Period& inactive_timeout_) : 
        index(std::numeric_limits<size_t>::max()),
        operational(true), 
        installed(false), 
        join_message(0), 
        leave_message(0),
        inactive_timeout(inactive_timeout_),
        tstamp(gu::datetime::Date::now()),
        fifo_seq(-1)
    {}

    Node(const Node& n);
    
    ~Node();
    
    void set_index(const size_t idx) { index = idx; }
    size_t get_index() const { return index; }
    
    void set_operational(const bool op) 
    { 
        gcomm_assert(op == false);
        operational = op; 
    }
    bool get_operational() const { return operational; }
    
    void set_installed(const bool inst) { installed = inst; }
    bool get_installed() const { return installed; }
    
    void set_join_message(const JoinMessage* msg);
    
    const JoinMessage* get_join_message() const { return join_message; }
    
    void set_leave_message(const LeaveMessage* msg);
    
    const LeaveMessage* get_leave_message() const { return leave_message; }
    
    void set_tstamp(const gu::datetime::Date& t) { tstamp = t; }
    const gu::datetime::Date& get_tstamp() const { return tstamp; }
    
    void set_fifo_seq(const int64_t seq) { fifo_seq = seq; }
    int64_t get_fifo_seq() const { return fifo_seq; }
    
    bool is_inactive() const;

private:
    
    
    
    void operator=(const Node&);
    
    // Index for input map
    size_t index;
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    JoinMessage* join_message;
    // Last activity timestamp
    LeaveMessage* leave_message;
    // 
    gu::datetime::Period inactive_timeout;
    // 
    gu::datetime::Date tstamp;
    //
    int64_t fifo_seq;
    
};

class gcomm::evs::NodeMap : public Map<UUID, Node> { };


class gcomm::evs::OperationalSelect
{
public:
    OperationalSelect(NodeMap& nm_) : nm(nm_) { }
    
    void operator()(const NodeMap::value_type& vt) const
    {
        if (NodeMap::get_value(vt).get_operational() == true)
        {
            nm.insert_checked(vt);
        }
    }
private:
    NodeMap& nm;
};



#endif // EVS_NODE_HPP
