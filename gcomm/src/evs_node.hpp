/*
 * Copyright (C) 2009-2019 Codership Oy <info@codership.com>
 */

#ifndef EVS_NODE_HPP
#define EVS_NODE_HPP

#include "evs_message2.hpp"


#include "gcomm/map.hpp"
#include "gcomm/uuid.hpp"
#include "gu_datetime.hpp"
#include "gu_logger.hpp"

#include <limits>

#include <stdint.h>



namespace gcomm
{
    namespace evs
    {
        class Node;
        class NodeMap;
        std::ostream& operator<<(std::ostream&, const Node&);
        class InspectNode;
        class OperationalSelect;
        class Proto;
    }
}


class gcomm::evs::Node
{
public:
    static const size_t invalid_index;
    Node(const Proto& proto)
        :
        proto_             (proto),
        index_             (invalid_index),
        operational_       (true),
        suspected_         (false),
        inactive_          (false),
        committed_         (false),
        installed_         (false),
        join_message_      (0),
        leave_message_     (0),
        delayed_list_message_(0),
        tstamp_            (gu::datetime::Date::monotonic()),
        seen_tstamp_       (tstamp_),
        last_requested_range_tstamp_(),
        last_requested_range_(),
        fifo_seq_          (-1),
        segment_           (0)
    {}

    Node(const Node& n);

    ~Node();

    void set_index(const size_t idx) { index_ = idx; }
    size_t index() const { return index_; }

    void set_operational(const bool op)
    {
        gcomm_assert(op == false);
        operational_ = op;
    }
    bool operational() const { return operational_; }

    void set_suspected(const bool s)
    {
        suspected_ = s;
    }
    bool suspected() const { return suspected_; }

    void set_committed(const bool comm) { committed_ = comm; }
    bool committed() const { return committed_; }
    void set_installed(const bool inst) { installed_ = inst; }
    bool installed() const { return installed_; }

    void set_join_message(const JoinMessage* msg);

    const JoinMessage* join_message() const { return join_message_; }

    void set_leave_message(const LeaveMessage* msg);

    const LeaveMessage* leave_message() const { return leave_message_; }

    void set_delayed_list_message(const DelayedListMessage* msg);

    const DelayedListMessage *delayed_list_message() const
    { return delayed_list_message_; }


    void set_tstamp(const gu::datetime::Date& t) { tstamp_ = t; }
    const gu::datetime::Date& tstamp() const { return tstamp_; }

    void set_seen_tstamp(const gu::datetime::Date& t) { seen_tstamp_ = t; }
    const gu::datetime::Date& seen_tstamp() const { return seen_tstamp_; }

    void last_requested_range(const Range& range)
    {
        assert(range.is_empty() == false);
        last_requested_range_tstamp_ = gu::datetime::Date::monotonic();
        last_requested_range_ = range;
    }
    gu::datetime::Date last_requested_range_tstamp() const
    { return last_requested_range_tstamp_; }
    const Range& last_requested_range() const { return last_requested_range_; }

    void set_fifo_seq(const int64_t seq) { fifo_seq_ = seq; }
    int64_t fifo_seq() const { return fifo_seq_; }
    SegmentId segment() const { return segment_; }

    bool is_inactive() const;
    bool is_suspected() const;

private:

    void operator=(const Node&);

    friend class InspectNode;

    const Proto& proto_;
    // Index for input map
    size_t index_;
    // True if instance is considered to be operational (has produced messages)
    bool operational_;
    bool suspected_;
    bool inactive_;
    // True if it is known that the instance has committed to install message
    bool committed_;
    // True if it is known that the instance has installed current view
    bool installed_;
    // Last received JOIN message
    JoinMessage* join_message_;
    // Leave message
    LeaveMessage* leave_message_;
    // Delayed list message
    DelayedListMessage* delayed_list_message_;
    // Timestamp denoting the last time a message from node
    // advanced input map state or membership protocol. This is used
    // for determining if the node should become suspected/inactive.
    gu::datetime::Date tstamp_;
    // Timestamp denoting the time when the node was seen last time.
    // This is used to decide if the node should be considered delayed.
    gu::datetime::Date seen_tstamp_;
    // Last time the gap message requesting a message resend/recovery
    // was sent to this node.
    gu::datetime::Date last_requested_range_tstamp_;
    // Last requested (non-empty) range.
    Range last_requested_range_;
    int64_t fifo_seq_;
    SegmentId segment_;
};

class gcomm::evs::NodeMap : public Map<UUID, Node> { };


class gcomm::evs::OperationalSelect
{
public:
    OperationalSelect(NodeMap& nm_) : nm(nm_) { }

    void operator()(const NodeMap::value_type& vt) const
    {
        if (NodeMap::value(vt).operational() == true)
        {
            nm.insert_unique(vt);
        }
    }
private:
    NodeMap& nm;
};


class gcomm::evs::InspectNode
{
public:
    void operator()(std::pair<const gcomm::UUID, Node>& p) const;
};


#endif // EVS_NODE_HPP
