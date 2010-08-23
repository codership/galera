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
    Node(const gu::datetime::Period& inactive_timeout,
         const gu::datetime::Period& suspect_timeout) 
        : 
        index_           (std::numeric_limits<size_t>::max()),
        operational_     (true), 
        committed_       (false),
        installed_       (false), 
        join_message_    (0), 
        leave_message_   (0),
        suspect_timeout_ (suspect_timeout),
        inactive_timeout_(inactive_timeout),
        tstamp_          (gu::datetime::Date::now()),
        fifo_seq_        (-1)
    {}
    
    Node(const Node& n);
    
    ~Node();
    
    void set_index(const size_t idx) { index_ = idx; }
    size_t get_index() const { return index_; }
    
    void set_operational(const bool op) 
    { 
        gcomm_assert(op == false);
        operational_ = op; 
    }
    bool get_operational() const { return operational_; }
    
    void set_committed(const bool comm) { committed_ = comm; }
    bool get_committed() const { return committed_; }
    void set_installed(const bool inst) { installed_ = inst; }
    bool get_installed() const { return installed_; }

    void set_join_message(const JoinMessage* msg);
    
    const JoinMessage* get_join_message() const { return join_message_; }
    
    void set_leave_message(const LeaveMessage* msg);
    
    const LeaveMessage* get_leave_message() const { return leave_message_; }
    
    void set_tstamp(const gu::datetime::Date& t) { tstamp_ = t; }
    const gu::datetime::Date& get_tstamp() const { return tstamp_; }
    
    void set_fifo_seq(const int64_t seq) { fifo_seq_ = seq; }
    int64_t get_fifo_seq() const { return fifo_seq_; }
    
    bool is_inactive() const;
    bool is_suspected() const;
private:
    
    void operator=(const Node&);
    
    // Index for input map
    size_t index_;
    // True if instance is considered to be operational (has produced messages)
    bool operational_;
    // True if it is known that the instance has committed to install message
    bool committed_;
    // True if it is known that the instance has installed current view
    bool installed_;
    // Last received JOIN message
    JoinMessage* join_message_;
    // Last activity timestamp
    LeaveMessage* leave_message_;
    gu::datetime::Period suspect_timeout_;
    // 
    gu::datetime::Period inactive_timeout_;
    // 
    gu::datetime::Date tstamp_;
    //
    int64_t fifo_seq_;
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
            nm.insert_unique(vt);
        }
    }
private:
    NodeMap& nm;
};



#endif // EVS_NODE_HPP
