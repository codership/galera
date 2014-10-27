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
#include "gu_histogram.hpp"
#include "gu_stats.hpp"
#include "profile.hpp"

#include "evs_seqno.hpp"
#include "evs_node.hpp"
#include "evs_consensus.hpp"
#include "protocol_version.hpp"

#include "gu_datetime.hpp"

#include <list>
#include <deque>
#include <vector>
#include <limits>

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

        //
        // Helper class for getting the location where
        // certain methods are called from.
        //
        // Example usage:
        // Method prototype:
        // void fun(EVS_CALLER_ARG, int a)
        //
        // Calling:
        // fun(EVS_CALLER, a)
        //
        // Logging inside function:
        // log_debug << EVS_LOG_METHOD << "log message"
        //
        class Caller
        {
        public:
            Caller(const char* const file, const int line) :
                file_(file),
                line_(line)
            { }
            friend std::ostream& operator<<(std::ostream&, const Caller&);
        private:
            const char* const file_;
            const int         line_;
        };
        inline std::ostream& operator<<(std::ostream& os, const Caller& caller)
        {
            return (os << caller.file_ << ": " << caller.line_ << ": ");
        }
#define EVS_CALLER_ARG const Caller& caller
#define EVS_CALLER Caller(__FILE__, __LINE__)
#define EVS_LOG_METHOD __FUNCTION__ << " called from " << caller
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
        }
    }

    friend std::ostream& operator<<(std::ostream&, const Proto&);
    friend class Consensus;

    /*!
     * Default constructor.
     */
    Proto(gu::Config&    conf,
          const UUID&    my_uuid,
          SegmentId      segment,
          const gu::URI& uri = gu::URI("evs://"),
          const size_t   mtu = std::numeric_limits<size_t>::max(),
          const View*    rst_view = NULL);
    ~Proto();

    const UUID& uuid() const { return my_uuid_; }

    std::string self_string() const
    {
        std::ostringstream os;
        os << "evs::proto(" << uuid() << ", " << to_string(state())
           << ", " << current_view_.id() << ")";
        return os.str();
    }

    State state() const { return state_; }

    size_t known_size() const { return known_.size(); }

    bool is_output_empty() const { return output_.empty(); }

    std::string stats() const;
    void reset_stats();

    bool is_flow_control(const seqno_t, const seqno_t win) const;
    int send_user(Datagram&,
                  uint8_t,
                  Order,
                  seqno_t,
                  seqno_t,
                  size_t n_aggregated = 1);
    size_t mtu() const { return mtu_; }
    size_t aggregate_len() const;
    int send_user(const seqno_t);
    void complete_user(const seqno_t);
    int send_delegate(Datagram&);
    void send_gap(EVS_CALLER_ARG,
                  const UUID&, const ViewId&, const Range,
                  bool commit = false, bool req_all = false);
    const JoinMessage& create_join();
    void send_join(bool tval = true);
    void set_join(const JoinMessage&, const UUID&);
    void set_leave(const LeaveMessage&, const UUID&);
    void send_leave(bool handle = true);
    void send_install(EVS_CALLER_ARG);
    void send_delayed_list();

    void resend(const UUID&, const Range);
    void recover(const UUID&, const UUID&, const Range);

    void retrans_user(const UUID&, const MessageNodeList&);
    void retrans_leaves(const MessageNodeList&);

    void set_inactive(const UUID&);
    bool is_inactive(const UUID&) const;
    void check_inactive();
    // Clean up foreign nodes according to install message.
    void cleanup_foreign(const InstallMessage&);
    void cleanup_views();
    void cleanup_evicted();
    void cleanup_joins();

    size_t n_operational() const;

    void validate_reg_msg(const UserMessage&);
    void deliver_finish(const InputMapMsg&);
    void deliver();
    void deliver_local(bool trans = false);
    void deliver_causal(uint8_t user_type, seqno_t seqno, const Datagram&);
    void validate_trans_msg(const UserMessage&);
    void deliver_trans();
    void deliver_reg_view(const InstallMessage&, const View&);
    void deliver_trans_view(const InstallMessage&, const View&);
    void deliver_empty_view();

    void setall_committed(bool val);
    bool is_all_committed() const;
    void setall_installed(bool val);
    bool is_all_installed() const;
    bool is_install_message() const { return install_message_ != 0; }

    bool is_representative(const UUID& pid) const;

    void shift_to(const State, const bool send_j = true);
    bool is_all_suspected(const UUID& uuid) const;
    const View& current_view() const { return current_view_; }

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
    void check_unseen();
    void check_nil_view_id();
    void asymmetry_elimination();
    void handle_foreign(const Message&);
    void handle_user(const UserMessage&,
                     NodeMap::iterator,
                     const Datagram&);
    void handle_delegate(const DelegateMessage&,
                         NodeMap::iterator,
                         const Datagram&);
    void handle_gap(const GapMessage&, NodeMap::iterator);
    void handle_join(const JoinMessage&, NodeMap::iterator);
    void handle_leave(const LeaveMessage&, NodeMap::iterator);
    void handle_install(const InstallMessage&, NodeMap::iterator);
    void handle_delayed_list(const DelayedListMessage&, NodeMap::iterator);
    void populate_node_list(MessageNodeList*) const;
    void isolate(gu::datetime::Period period);
public:
    static size_t unserialize_message(const UUID&,
                                      const Datagram&,
                                      Message*);
    void handle_msg(const Message& msg,
                    const Datagram& dg = Datagram(),
                    bool direct = true);
    // Protolay
    void handle_up(const void*, const Datagram&, const ProtoUpMeta&);
    int handle_down(Datagram& wb, const ProtoDownMeta& dm);

    int send_down(Datagram& dg, const ProtoDownMeta& dm);

    void handle_stable_view(const View& view)
    {
        set_stable_view(view);
    }

    void handle_fencing(const UUID& uuid) { }

    void connect(bool first)
    {
        gu_trace(shift_to(S_JOINING));
        gu_trace(send_join(first));
    }

    void close(bool force = false)
    {
        // shifting to S_LEAVING from S_INSTALL is troublesome,
        // instead of that raise a boolean flag to indicate that
        // shifting to S_LEAVING should be done once S_OPERATIONAL
        // is reached
        //
        // #760 - pending leave should be done also from S_GATHER,
        // changing state to S_LEAVING resets timers and may prevent
        // remaining nodes to reach new group until install timer
        // times out
        log_debug << self_string() << " closing in state " << state();
        if (state() != S_GATHER && state() != S_INSTALL)
        {
            gu_trace(shift_to(S_LEAVING));
            gu_trace(send_leave());
            pending_leave_ = false;
        }
        else
        {
            pending_leave_ = true;
        }
    }

    void close(const UUID& uuid)
    {
        set_inactive(uuid);
    }

    bool set_param(const std::string& key, const std::string& val);

    void handle_get_status(gu::Status& status) const;

    // gu::datetime::Date functions do appropriate actions for timer handling
    // and return next expiration time
private:
public:
    enum Timer
    {
        T_INACTIVITY,
        T_RETRANS,
        T_INSTALL,
        T_STATS
    };
    /*!
     * Internal timer list
     */
    typedef MultiMap<gu::datetime::Date, Timer> TimerList;
private:
    TimerList timers_;
public:
    // These need currently to be public for unit tests
    void handle_inactivity_timer();
    void handle_retrans_timer();
    void handle_install_timer();
    void handle_stats_timer();
    gu::datetime::Date next_expiration(const Timer) const;
    void reset_timer(Timer);
    void cancel_timer(Timer);
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

    int version_;
    int debug_mask_;
    int info_mask_;
    gu::datetime::Date last_stats_report_;
    bool collect_stats_;
    gu::Histogram hs_agreed_;
    gu::Histogram hs_safe_;
    gu::Histogram hs_local_causal_;
    gu::Stats     safe_deliv_latency_;
    long long int send_queue_s_;
    long long int n_send_queue_s_;
    std::vector<long long int> sent_msgs_;
    long long int retrans_msgs_;
    long long int recovered_msgs_;
    std::vector<long long int> recvd_msgs_;
    std::vector<long long int> delivered_msgs_;
    prof::Profile send_user_prof_;
    prof::Profile send_gap_prof_;
    prof::Profile send_join_prof_;
    prof::Profile send_install_prof_;
    prof::Profile send_leave_prof_;
    prof::Profile consistent_prof_;
    prof::Profile consensus_prof_;
    prof::Profile shift_to_prof_;
    prof::Profile input_map_prof_;
    prof::Profile delivery_prof_;
    bool delivering_;
    UUID my_uuid_;
    SegmentId segment_;
    //
    // Known instances
    friend class Node;
    friend class InspectNode;
    NodeMap known_;
    NodeMap::iterator self_i_;
    //
    gu::datetime::Period view_forget_timeout_;
    gu::datetime::Period inactive_timeout_;
    gu::datetime::Period suspect_timeout_;
    gu::datetime::Period inactive_check_period_;
    gu::datetime::Period retrans_period_;
    gu::datetime::Period install_timeout_;
    gu::datetime::Period join_retrans_period_;
    gu::datetime::Period stats_report_period_;
    gu::datetime::Period causal_keepalive_period_;

    gu::datetime::Period delay_margin_;
    gu::datetime::Period delayed_keep_period_;

    gu::datetime::Date last_inactive_check_;
    gu::datetime::Date last_causal_keepalive_;

    // Current view id
    // ViewId current_view;
    View current_view_;
    View previous_view_;
    typedef std::map<ViewId, gu::datetime::Date> ViewList;
    // List of previously seen views from which messages should not be
    // accepted anymore
    ViewList previous_views_;
    // Seen views in gather state, will be copied to previous views
    // when shifting to operational
    ViewList gather_views_;

    // Map containing received messages and aru/safe seqnos
    InputMap* input_map_;
    // Helper container for local causal messages
    class CausalMessage
    {
    public:
        CausalMessage(uint8_t             user_type,
                      seqno_t             seqno,
                      const Datagram& datagram)
            :
            user_type_(user_type),
            seqno_    (seqno    ),
            datagram_ (datagram ),
            tstamp_   (gu::datetime::Date::now())
        { }
        uint8_t             user_type() const { return user_type_; }
        seqno_t             seqno()     const { return seqno_    ; }
        const Datagram& datagram()  const { return datagram_ ; }
        const gu::datetime::Date& tstamp()    const { return tstamp_   ; }
    private:
        uint8_t            user_type_;
        seqno_t            seqno_;
        Datagram       datagram_;
        gu::datetime::Date tstamp_;
    };
    // Queue containing local causal messages
    std::deque<CausalMessage> causal_queue_;
    // Consensus module
    Consensus consensus_;
    // Last received install message
    InstallMessage* install_message_;
    // Highest seen view id seqno
    uint32_t max_view_id_seq_;
    // Install attempt counter
    uint32_t attempt_seq_;
    // Install timeout counting
    int max_install_timeouts_;
    int install_timeout_count_;
    // Sequence number to maintain membership message FIFO order
    int64_t fifo_seq_;
    // Last sent seq
    seqno_t last_sent_;
    // Protocol send window size
    seqno_t send_window_;
    // User send window size
    seqno_t user_send_window_;
    // Output message queue
    std::deque<std::pair<Datagram, ProtoDownMeta> > output_;
    std::vector<gu::byte_t> send_buf_;
    uint32_t max_output_size_;
    size_t mtu_;
    bool use_aggregate_;
    bool self_loopback_;
    State state_;
    int shift_to_rfcnt_;
    bool pending_leave_;
    gu::datetime::Date isolation_end_;

    class DelayedEntry
    {
    public:
        typedef enum
        {
            S_OK,
            S_DELAYED
        } State;
        DelayedEntry(const std::string& addr)
            :
            addr_      (addr),
            tstamp_(gu::datetime::Date::now()),
            state_(S_DELAYED),
            state_change_cnt_(1)
        { }
        const std::string& addr() const { return addr_; }

        void set_tstamp(gu::datetime::Date tstamp) { tstamp_ = tstamp; }
        gu::datetime::Date tstamp() const { return tstamp_; }

        void set_state(State state,
                       const gu::datetime::Period decay_period,
                       const gu::datetime::Date now)
        {
            if (state == S_DELAYED && state_ != state)
            {
                // Limit to 0xff, see DelayedList format in DelayedListMessage
                // restricts this value to uint8_t max.
                if (state_change_cnt_ < 0xff)
                    ++state_change_cnt_;
            }
            else if (state == S_OK &&
                     tstamp_ + decay_period < now)
            {
                if (state_change_cnt_ > 0)
                    --state_change_cnt_;
            }
            state_ = state;
        }
        State state() const {return state_; }
        size_t state_change_cnt() const { return state_change_cnt_; }
    private:
        const std::string addr_;
        gu::datetime::Date tstamp_;
        State  state_;
        size_t state_change_cnt_;
    };

    typedef std::map<UUID, DelayedEntry> DelayedList;
    DelayedList delayed_list_;
    size_t      auto_evict_;

    // non-copyable
    Proto(const Proto&);
    void operator=(const Proto&);
};


#endif // EVS_PROTO_HPP
