/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * @file evs_proto.hpp
 *
 * @brief EVS protocol implementation header.
 */

#ifndef GCOMM_EVS_PROTO_HPP
#define GCOMM_EVS_PROTO_HPP

#include "gcomm/protolay.hpp"
#include "gcomm/view.hpp"
#include "gcomm/transport.hpp"
#include "gcomm/map.hpp"
#include "histogram.hpp"
#include "profile.hpp"

#include "evs_seqno.hpp"
#include "evs_node.hpp"
#include "evs_consensus.hpp"

#include "gu_datetime.hpp"

#include <list>
#include <deque>
#include <vector>
#include <limits>

#ifndef GCOMM_EVS_MAX_VERSION
#define GCOMM_EVS_MAX_VERSION 0
#endif // GCOMM_EVS_MAX_VERSION

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
        class InputMapMsg;
        class Proto;
        std::ostream& operator<<(std::ostream&, const Proto&);
    }
}


/*!
 * @brief Class implementing EVS protocol
 */
class gcomm::evs::Proto : public Protolay
{
public:
    enum State {
        S_CLOSED,
        S_JOINING,
        S_LEAVING,
        S_GATHER,
        S_INSTALL,
        S_OPERATIONAL,
        S_MAX
    };

    static std::string to_string(const State s)
    {
        switch (s) {
        case S_CLOSED:      return "CLOSED";
        case S_JOINING:     return "JOINING";
        case S_LEAVING:     return "LEAVING";
        case S_GATHER:      return "GATHER";
        case S_INSTALL:     return "INSTALL";
        case S_OPERATIONAL: return "OPERATIONAL";
        default:
            gu_throw_fatal << "Invalid state";
            throw;
        }
    }

    friend std::ostream& operator<<(std::ostream&, const Proto&);

    /*!
     * Default constructor.
     */
    Proto(gu::Config& conf,
          const UUID& my_uuid_, const gu::URI& uri = gu::URI("evs://"),
          const size_t mtu_ = std::numeric_limits<size_t>::max());
    ~Proto();

    const UUID& get_uuid() const { return my_uuid; }

    std::string self_string() const
    {
        std::ostringstream os;
        os << "evs::proto(" << get_uuid() << ", " << to_string(get_state())
           << ", " << current_view.get_id() << ")";
        return os.str();
    }

    State get_state() const { return state; }

    size_t get_known_size() const { return known.size(); }

    bool is_output_empty() const { return output.empty(); }

    std::string get_stats() const;
    void reset_stats();

    bool is_flow_control(const seqno_t, const seqno_t win) const;
    int send_user(gu::Datagram&,
                  uint8_t,
                  Order,
                  seqno_t,
                  seqno_t,
                  size_t n_aggregated = 1);
    size_t get_mtu() const { return mtu; }
    size_t aggregate_len() const;
    int send_user(const seqno_t);
    void complete_user(const seqno_t);
    int send_delegate(gu::Datagram&);
    void send_gap(const UUID&, const ViewId&, const Range, bool commit = false);
    const JoinMessage& create_join();
    void send_join(bool tval = true);
    void set_join(const JoinMessage&, const UUID&);
    void set_leave(const LeaveMessage&, const UUID&);
    void send_leave(bool handle = true);
    void send_install();

    void resend(const UUID&, const Range);
    void recover(const UUID&, const UUID&, const Range);

    void retrans_user(const UUID&, const MessageNodeList&);
    void retrans_leaves(const MessageNodeList&);


    void set_inactive(const UUID&);
    void check_inactive();
    void cleanup_unoperational();
    void cleanup_views();
    void cleanup_joins();

    size_t n_operational() const;

    void validate_reg_msg(const UserMessage&);
    void deliver_finish(const InputMapMsg&);
    void deliver();
    void deliver_local(bool trans = false);
    void deliver_causal(uint8_t user_type, seqno_t seqno, const gu::Datagram&);
    void validate_trans_msg(const UserMessage&);
    void deliver_trans();
    void deliver_reg_view();
    void deliver_trans_view(bool local);
    void deliver_empty_view();

    void setall_committed(bool val);
    bool is_all_committed() const;
    void setall_installed(bool val);
    bool is_all_installed() const;


    bool is_representative(const UUID& pid) const;

    void shift_to(const State, const bool send_j = true);


    // Message handlers
private:


    /*!
     * Update input map safe seq
     * @param uuid Node uuid
     * @param seq  Sequence number
     * @return Input map seqno before updating
     */
    seqno_t update_im_safe_seq(const size_t uuid, const seqno_t seq);

    /*!
     * Update input map safe seqs according to message node list. Only
     * inactive nodes are allowed to be in
     */
    bool update_im_safe_seqs(const MessageNodeList&);
    bool is_msg_from_previous_view(const Message&);
    void check_suspects(const UUID&, const MessageNodeList&);
    void cross_check_inactives(const UUID&, const MessageNodeList&);
    void handle_foreign(const Message&);
    void handle_user(const UserMessage&,
                     NodeMap::iterator,
                     const gu::Datagram&);
    void handle_delegate(const DelegateMessage&,
                         NodeMap::iterator,
                         const gu::Datagram&);
    void handle_gap(const GapMessage&, NodeMap::iterator);
    void handle_join(const JoinMessage&, NodeMap::iterator);
    void handle_leave(const LeaveMessage&, NodeMap::iterator);
    void handle_install(const InstallMessage&, NodeMap::iterator);
    void populate_node_list(MessageNodeList*) const;
public:
    static size_t unserialize_message(const UUID&,
                                      const gu::Datagram&,
                                      Message*);
    void handle_msg(const Message& msg,
                    const gu::Datagram& dg = gu::Datagram());
    // Protolay
    void handle_up(const void*, const gu::Datagram&, const ProtoUpMeta&);
    int handle_down(gu::Datagram& wb, const ProtoDownMeta& dm);
    void handle_stable_view(const View& view)
    {
        set_stable_view(view);
    }
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

    void close(const UUID& uuid)
    {
        set_inactive(uuid);
    }

    // gu::datetime::Date functions do appropriate actions for timer handling
    // and return next expiration time
private:
public:
    enum Timer
    {
        T_INACTIVITY,
        T_RETRANS,
        T_CONSENSUS,
        T_INSTALL,
        T_STATS
    };
    /*!
     * Internal timer list
     */
    class TimerList :
        public  MultiMap<gu::datetime::Date, Timer> { };
private:
    TimerList timers;
public:
    // These need currently to be public for unit tests
    void handle_inactivity_timer();
    void handle_retrans_timer();
    void handle_consensus_timer();
    void handle_install_timer();
    void handle_stats_timer();
    gu::datetime::Date get_next_expiration(const Timer) const;
    void reset_timers();
    gu::datetime::Date handle_timers();

    /*!
     * @brief Flags controlling what debug information is logged if
     *        debug logging is turned on.
     */
    enum DebugFlags
    {
        D_STATE         = 1 << 0,  /*!< State changes */
        D_TIMERS        = 1 << 1,  /*!< Timer handling */
        D_CONSENSUS     = 1 << 2,  /*!< Consensus protocol */
        D_USER_MSGS     = 1 << 3,  /*!< User messages */
        D_DELEGATE_MSGS = 1 << 4,  /*!< Delegate messages */
        D_GAP_MSGS      = 1 << 5,  /*!< Gap messages */
        D_JOIN_MSGS     = 1 << 6,  /*!< Join messages */
        D_INSTALL_MSGS  = 1 << 7,  /*!< Install messages */
        D_LEAVE_MSGS    = 1 << 8,  /*!< Leave messages */
        D_FOREIGN_MSGS  = 1 << 9,  /*!< Foreing messages */
        D_RETRANS       = 1 << 10, /*!< Retransmitted/recovered messages */
        D_DELIVERY      = 1 << 11  /*!< Message delivery */
    };

    /*!
     * @brief Flags controlling what info log is printed in logs.
     */
    enum InfoFlags
    {
        I_VIEWS      = 1 << 0, /*!< View changes */
        I_STATE      = 1 << 1, /*!< State change information */
        I_STATISTICS = 1 << 2, /*!< Statistics */
        I_PROFILING  = 1 << 3  /*!< Profiling information */
    };
private:

    int version;
    static const int max_version_ = GCOMM_EVS_MAX_VERSION;
    int debug_mask;
    int info_mask;
    gu::datetime::Date last_stats_report;
    bool collect_stats;
    Histogram hs_agreed;
    Histogram hs_safe;
    Histogram hs_local_causal;
    long long int send_queue_s;
    long long int n_send_queue_s;
    std::vector<long long int> sent_msgs;
    long long int retrans_msgs;
    long long int recovered_msgs;
    std::vector<long long int> recvd_msgs;
    std::vector<long long int> delivered_msgs;
    prof::Profile send_user_prof;
    prof::Profile send_gap_prof;
    prof::Profile send_join_prof;
    prof::Profile send_install_prof;
    prof::Profile send_leave_prof;
    prof::Profile consistent_prof;
    prof::Profile consensus_prof;
    prof::Profile shift_to_prof;
    prof::Profile input_map_prof;
    prof::Profile delivery_prof;
    bool delivering;
    UUID my_uuid;
    //
    // Known instances
    NodeMap known;
    NodeMap::iterator self_i;
    //
    gu::datetime::Period view_forget_timeout;
    gu::datetime::Period inactive_timeout;
    gu::datetime::Period suspect_timeout;
    gu::datetime::Period inactive_check_period;
    gu::datetime::Period consensus_timeout;
    gu::datetime::Period retrans_period;
    gu::datetime::Period install_timeout;
    gu::datetime::Period join_retrans_period;
    gu::datetime::Period stats_report_period;

    gu::datetime::Date last_inactive_check;

    // Current view id
    // ViewId current_view;
    View current_view;
    View previous_view;
    std::list<std::pair<ViewId, gu::datetime::Date> > previous_views;

    // Map containing received messages and aru/safe seqnos
    InputMap* input_map;
    // Helper container for local causal messages
    class CausalMessage
    {
    public:
        CausalMessage(uint8_t             user_type,
                      seqno_t             seqno,
                      const gu::Datagram& datagram)
            :
            user_type_(user_type),
            seqno_    (seqno    ),
            datagram_ (datagram ),
            tstamp_   (gu::datetime::Date::now())
        { }
        uint8_t             user_type() const { return user_type_; }
        seqno_t             seqno()     const { return seqno_    ; }
        const gu::Datagram& datagram()  const { return datagram_ ; }
        const gu::datetime::Date& tstamp()    const { return tstamp_   ; }
    private:
        uint8_t      user_type_;
        seqno_t      seqno_;
        gu::Datagram datagram_;
        gu::datetime::Date     tstamp_;
    };
    // Queue containing local causal messages
    std::deque<CausalMessage> causal_queue_;
    // Consensus module
    Consensus consensus;
    // Consensus attempt count
    size_t cac;
    // Last received install message
    InstallMessage* install_message;
    // Install attempt counter
    uint32_t attempt_seq;
    // Sequence number to maintain membership message FIFO order
    int64_t fifo_seq;
    // Last sent seq
    seqno_t last_sent;
    // Protocol send window size
    seqno_t send_window;
    // User send window size
    seqno_t user_send_window;
    // Output message queue
    std::deque<std::pair<gu::Datagram, ProtoDownMeta> > output;
    std::vector<gu::byte_t> send_buf_;
    uint32_t max_output_size;
    size_t mtu;
    bool use_aggregate;

    bool self_loopback;
    State state;
    int shift_to_rfcnt;


    Proto(const Proto&);
    void operator=(const Proto&);
};



#endif // EVS_PROTO_HPP
