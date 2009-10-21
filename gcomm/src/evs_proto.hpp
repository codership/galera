#ifndef EVS_PROTO_HPP
#define EVS_PROTO_HPP

#include "gcomm/common.hpp"
#include "gcomm/time.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/view.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/map.hpp"
#include "histogram.hpp"

#include "evs_seqno.hpp"

#include <list>
#include <deque>

namespace gcomm
{
    namespace evs
    {

        class Message;
        class MessageNodeList;
        class UserMessage;
        class DelegateMessage;
        class GapMessage;
        class JoinMessage;
        class InstallMessage;
        class LeaveMessage;
        class InputMap;

        class Node;
        std::ostream& operator<<(std::ostream&, const Node&);
        class NodeMap;
        class Proto;
        std::ostream& operator<<(std::ostream&, const Proto&);
    }
}

class gcomm::evs::Node
{
public:    
    Node() : 
        operational(true), 
        installed(false), 
        join_message(0), 
        leave_message(0),
        tstamp(Time::now()),
        fifo_seq(-1)
    {}

    Node(const Node& n);

    ~Node();
    
    void set_operational(const bool op) { operational = op; }
    bool get_operational() const { return operational; }
    
    void set_installed(const bool inst) { installed = inst; }
    bool get_installed() const { return installed; }
    
    void set_join_message(const JoinMessage* msg);
    
    const JoinMessage* get_join_message() const { return join_message; }
    
    void set_leave_message(const LeaveMessage* msg);
    
    const LeaveMessage* get_leave_message() const { return leave_message; }
    
    void set_tstamp(const Time& t) { tstamp = t; }
    const Time& get_tstamp() const { return tstamp; }
    
    void set_fifo_seq(const int64_t seq) { fifo_seq = seq; }
    int64_t get_fifo_seq() const { return fifo_seq; }
    

private:



    void operator=(const Node&);


    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    JoinMessage* join_message;
    // Last activity timestamp
    LeaveMessage* leave_message;
    // 
    Time tstamp;
    //
    int64_t fifo_seq;
};

class gcomm::evs::NodeMap : public Map<UUID, Node> { };


class gcomm::evs::Proto : public Protolay
{
public:
    enum State {
        S_CLOSED,
        S_JOINING,
        S_LEAVING,
        S_RECOVERY, 
        S_OPERATIONAL,
        S_MAX
    };
    friend std::ostream& operator<<(std::ostream&, const Proto&);
public:

    Proto(const UUID& my_uuid_, const std::string& conf = "evs://");
    ~Proto();
    
    const UUID& get_uuid() const { return my_uuid; }


    std::string self_string() const 
    { 
        return "evs::proto(" + get_uuid().to_string() + ","
            + to_string(get_state()) + "," + gu::to_string(current_view.get_id()) + ")"; 
    }
    
    State get_state() const { return state; }
    
    size_t get_known_size() const { return known.size(); }
    
    bool is_output_empty() const { return output.empty(); }
    
    static std::string to_string(const State s) 
    {
        switch (s) {
        case S_CLOSED:      return "CLOSED";
        case S_JOINING:     return "JOINING";
        case S_LEAVING:     return "LEAVING";
        case S_RECOVERY:    return "RECOVERY";
        case S_OPERATIONAL: return "OPERATIONAL";
        default:
            gcomm_throw_fatal << "Invalid state";
            throw;
        }
    }
    
    bool is_flow_control(const Seqno, const Seqno win) const;
    int send_user(WriteBuf* wb, const uint8_t,
                  SafetyPrefix sp, 
                  const Seqno, 
                  const Seqno, bool local = false);
    int send_user();
    void complete_user(const Seqno);
    int send_delegate(WriteBuf*);
    void send_gap(const UUID&, const ViewId&, const Range);
    JoinMessage create_join();
    void send_join(bool tval = true);
    void set_join(const JoinMessage&, const UUID&);
    void set_leave(const LeaveMessage&, const UUID&);
    void send_leave();
    void send_install();
    
    void resend(const UUID&, const Range);
    void recover(const UUID&, const UUID&, const Range);
    
    void set_inactive(const UUID&);
    void check_inactive();
    void cleanup_unoperational();
    void cleanup_views();
    void cleanup_joins();

    size_t n_operational() const;
    
    void validate_reg_msg(const UserMessage&);
    void deliver();
    void validate_trans_msg(const UserMessage&);
    void deliver_trans();
    void deliver_reg_view();
    void deliver_trans_view(bool local);
    void deliver_empty_view();

    void setall_installed(bool val);


    bool is_all_installed() const;
    
    // Compares join message against current state
    
    bool is_consistent(const Message&) const;
    bool is_consistent_input_map(const Message&) const;
    bool is_consistent_joining(const Message&) const;
    bool is_consistent_partitioning(const Message&) const;
    bool is_consistent_leaving(const Message&) const;
    bool is_consistent_same_view(const Message&) const;
    bool is_consensus() const;
    
    bool is_representative(const UUID& pid) const;

    void shift_to(const State, const bool send_j = true);
    
    
    // Message handlers
private:
    void handle_foreign(const Message&);
    void handle_user(const UserMessage&, 
                     NodeMap::iterator, 
                     const ReadBuf*, 
                     const size_t);
    void handle_delegate(const DelegateMessage&, 
                         NodeMap::iterator,
                         const ReadBuf*, 
                         size_t);
    void handle_gap(const GapMessage&, NodeMap::iterator);
    void handle_join(const JoinMessage&, NodeMap::iterator);
    void handle_leave(const LeaveMessage&, NodeMap::iterator);
    void handle_install(const InstallMessage&, NodeMap::iterator);
    void populate_node_list(MessageNodeList*) const;
public:
    static size_t unserialize_message(const UUID&, const ReadBuf*, size_t,
                                      Message*);
    void handle_msg(const Message& msg, 
                    const ReadBuf* rb = 0, const size_t roff = 0);    
    // Protolay
    void handle_up(int, const ReadBuf*, size_t, const ProtoUpMeta&);
    int handle_down(WriteBuf* wb, const ProtoDownMeta& dm);
    void connect(bool first)
    {
        gu_trace(shift_to(S_JOINING));
        gu_trace(send_join(first));
    }

    void close()
    {
        gu_trace(shift_to(S_LEAVING));
        gu_trace(send_leave());
    }

    // Timer functions do appropriate actions for timer handling 
    // and return next expiration time 
private:
public:
    enum Timer
    {
        T_INACTIVITY,
        T_RETRANS,
        T_CONSENSUS
    };
    class TimerList : public  MultiMap<Time, Timer> { };
private:
    TimerList timers;
public:
    // These need currently to be public for unit tests
    void handle_inactivity_timer();
    void handle_retrans_timer();
    void handle_consensus_timer();
    Time get_next_expiration(Timer) const;
    void reset_timers();
    Time handle_timers();
private:
    bool collect_stats;
    Histogram hs_safe;
    bool delivering;
    UUID my_uuid;
    // 
    // Known instances 
    NodeMap known;
    NodeMap::iterator self_i;
    // 
    Period view_forget_timeout;
    Period inactive_timeout;
    Period inactive_check_period;
    Period consensus_timeout;
    Period retrans_period;
    Period join_retrans_period;
    
    // Current view id
    // ViewId current_view;
    View current_view;
    View previous_view;
    std::list<std::pair<ViewId, Time> > previous_views;
    
    // Map containing received messages and aru/safe seqnos
    InputMap* input_map;
    
    // Last received install message
    InstallMessage* install_message;
    // Sequence number to maintain membership message FIFO order
    int64_t fifo_seq;
    // Last sent seq
    Seqno last_sent;
    // Send window size
    Seqno send_window;
    // Output message queue
    std::deque<std::pair<WriteBuf*, ProtoDownMeta> > output;
    
    uint32_t max_output_size;
    
    
    bool self_loopback;
    State state;
    int shift_to_rfcnt;


    Proto(const Proto&);
    void operator=(const Proto&);
};



#endif // EVS_PROTO_HPP
