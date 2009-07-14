#ifndef EVS_PROTO_HPP
#define EVS_PROTO_HPP

#include "gcomm/common.hpp"
#include "evs_input_map.hpp"
#include "evs_message.hpp"
#include "gcomm/time.hpp"
#include "gcomm/timer.hpp"
#include "gcomm/protolay.hpp"
#include "gcomm/view.hpp"
#include "gcomm/transport.hpp"
#include "inst_map.hpp"
#include "histogram.hpp"

#include <set>
#include <list>

using std::set;
using std::list;
using std::deque;
using std::pair;
using std::make_pair;


BEGIN_GCOMM_NAMESPACE

struct EVSInstance 
{
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    EVSMessage* join_message;
    // Last activity timestamp
    EVSMessage* leave_message;
    // 
    Time tstamp;
    //
    EVSRange prev_range;
    // greatest seen FIFO seqno from this source 
    int64_t fifo_seq;
    // CTOR
    EVSInstance() : 
        operational(true), 
        installed(false), 
        join_message(0), 
        leave_message(0),
        tstamp(Time::now()),
        prev_range(SEQNO_MAX, SEQNO_MAX),
        fifo_seq(-1)
    {
    }

    EVSInstance(const EVSInstance& i) :
        operational(i.operational),
        installed(i.installed),
        join_message(i.join_message),
        leave_message(i.leave_message),
        tstamp(i.tstamp),
        prev_range(i.prev_range),
        fifo_seq(i.fifo_seq)
    {
    }
    
    ~EVSInstance() 
    {
        delete join_message;
        delete leave_message;
    }
    
    bool get_operational() const
    {
        return operational;
    }

    bool get_installed() const
    {
        return installed;
    }

    string to_string() const 
    {
        std::string ret;
        ret += "o=";
        ret += (operational ? "1" : "0");
        ret += ",i=";
        ret += (installed ? "1" : "0");
        ret += ",l=";
        ret += (leave_message ? "1" : "0");
        return ret;
    }

    void update_tstamp() {
        tstamp = Time::now();
    }
private:

    void operator=(const EVSInstance&);
};



class EVSProto : public Protolay
{
public:
    // typedef InstMap<EVSInstance> EVSInstMap;
    struct EVSInstMap : InstMap<EVSInstance>
    {
    };

    enum State {
        CLOSED,
        JOINING,
        LEAVING,
        RECOVERY, 
        OPERATIONAL,
        STATE_MAX
    };
private:
    Monitor* mon;
    Transport* tp;
    EventLoop* el;
    bool collect_stats;
    Histogram hs_safe;
    bool delivering;
    UUID my_addr;
    // 
    // Known instances 
    EVSInstMap known;
    EVSInstMap::iterator self_i;
    // 
    Time inactive_timeout;
    Period inactive_check_period;
    Time consensus_timeout;
    Period resend_period;
    Period send_join_period;
    Timer timer;
    
    // Current view id
    // ViewId current_view;
    View current_view;
    View previous_view;
    list<pair<ViewId, Time> > previous_views;
    
    // Map containing received messages and aru/safe seqnos
    EVSInputMap input_map;
    
    // Last received install message
    EVSMessage* install_message;
    // 
    bool installing;
    // Sequence number to maintain membership message FIFO order
    int64_t fifo_seq;
    // Last sent seq
    uint32_t last_sent;
    // Send window size
    uint32_t send_window;
    // Output message queue
    deque<pair<WriteBuf*, ProtoDownMeta> > output;
    
    uint32_t max_output_size;


    bool self_loopback;
    State state;
    int shift_to_rfcnt;

    EVSProto(const EVSProto&);
    void operator=(const EVSProto&);
public:
    EVSProto(EventLoop* el_, Transport* t, const UUID& my_addr_, 
             const string& name, Monitor* mon_) : 
        mon(mon_),
        tp(t),
        el(el_),
        collect_stats(true),
        hs_safe("0.0,0.0005,0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.5,1.,5.,10.,30."),
        delivering(false),
        my_addr(my_addr_), 
        known(),
        self_i(),
        inactive_timeout(Time(5, 0)),
        inactive_check_period(Time(1, 0)),
        consensus_timeout(Time(1, 0)),
        resend_period(Time(1, 0)),
        send_join_period(Time(0, 300000)),
        timer(el_),
        current_view(View::V_TRANS, ViewId(my_addr, 0)),
        previous_view(),
        previous_views(),
        input_map(),
        install_message(0),
        installing(false),
        fifo_seq(-1),
        last_sent(SEQNO_MAX),
        send_window(8), 
        output(),
        max_output_size(1024),
        self_loopback(false),
        state(CLOSED),
        shift_to_rfcnt(0),
        ith(), cth(), consth(), resendth(), sjth()
    {
        LOG_DEBUG("EVSProto(): (" + my_addr.to_string() + ")");
        pair<std::map<const UUID, EVSInstance>::iterator, bool> i =
            known.insert(make_pair(my_addr, EVSInstance()));
        assert(i.second == true);
        self_i = i.first;
        assert(EVSInstMap::get_instance(self_i).get_operational() == true);
        
        input_map.insert_sa(my_addr);
        current_view.add_member(my_addr, "");
        
        ith = new InactivityTimerHandler(*this);
        cth = new CleanupTimerHandler(*this);
        consth = new ConsensusTimerHandler(*this);
        resendth = new ResendTimerHandler(*this);
        sjth = new SendJoinTimerHandler(*this);
    }
    
    ~EVSProto() {
        for (deque<pair<WriteBuf*, ProtoDownMeta> >::iterator i = output.begin(); i != output.end();
             ++i)
        {
            delete i->first;
        }
        output.clear();
        delete install_message;
        delete ith;
        delete cth;
        delete consth;
        delete resendth;
        delete sjth;
    }
    

    
    const UUID& get_uuid() const
    {
        return my_addr;
    }


    string self_string() const
    {
        return "(" + my_addr.to_string() + ")";
    }
    
    State get_state() const {
        return state;
    }

    size_t get_known_size() const
    {
        return known.length();
    }
    
    bool is_output_empty() const
    {
        return output.empty();
    }
    
    static std::string to_string(const State s) {
        switch (s) {
        case CLOSED:
            return "CLOSED";
        case JOINING:
            return "JOINING";
        case LEAVING:
            return "LEAVING";
        case RECOVERY:
            return "RECOVERY";
        case OPERATIONAL:
            return "OPERATIONAL";
        default:
            throw FatalException("Invalid state");
        }
    }
    
    bool is_flow_control(const uint32_t seq, const uint32_t win) const;
    int send_user(WriteBuf* wb, const uint8_t,
                  EVSSafetyPrefix sp, 
                  const uint32_t, 
                  const uint32_t, bool local = false);
    int send_user();
    void complete_user(const uint32_t);
    int send_delegate(const UUID&, WriteBuf*);
    void send_gap(const UUID&, const ViewId&, const EVSRange&);
    EVSJoinMessage create_join();
    void send_join(bool tval = true);
    void set_join(const EVSMessage&, const UUID&);
    void set_leave(const EVSMessage&, const UUID&);
    bool has_leave(const UUID&) const;
    void send_leave();
    void send_install();
    
    void resend(const UUID&, const EVSGap&);
    void recover(const EVSGap&);
    
    void set_inactive(const UUID&);
    void check_inactive();
    void cleanup_unoperational();
    void cleanup_views();
    void cleanup_joins();

    size_t n_operational() const;

    void validate_reg_msg(const EVSMessage&);
    void deliver();
    void validate_trans_msg(const EVSMessage&);
    void deliver_trans();
    void deliver_reg_view();
    void deliver_trans_view(bool local);
    void deliver_empty_view();

    void setall_installed(bool val);


    bool is_all_installed() const;
    
    // Compares join message against current state
    
    bool is_consistent(const EVSMessage& jm) const;
    
    bool is_consensus() const;
    
    bool is_representative(const UUID& pid) const;

    bool states_compare(const EVSMessage& );
    

    void shift_to(const State, const bool send_j = true);
    
    
    // Message handlers
private:
    void handle_foreign(const EVSMessage&);
    void handle_user(const EVSMessage&, 
                     EVSInstMap::iterator, 
                     const ReadBuf*, 
                     const size_t);
    void handle_delegate(const EVSMessage&, 
                         EVSInstMap::iterator,
                         const ReadBuf*, 
                         const size_t);
    void handle_gap(const EVSMessage&, EVSInstMap::iterator);
    void handle_join(const EVSMessage&, EVSInstMap::iterator);
    void handle_leave(const EVSMessage&, EVSInstMap::iterator);
    void handle_install(const EVSMessage&, EVSInstMap::iterator);
public:
    void handle_msg(const EVSMessage& msg, 
                    const ReadBuf* rb = 0, const size_t roff = 0);    
    // Protolay
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm);
    
    //
    void cleanup() {}



    class CleanupTimerHandler : public TimerHandler
    {
        EVSProto& p;
    public:
        CleanupTimerHandler(EVSProto& p_) : TimerHandler("cleanup"), p(p_) {}
        void handle() 
        {
            Critical crit(p.mon);
            p.cleanup_unoperational();
            p.cleanup_views();
            p.timer.set(this, Period(Time(30, 0)));
        }
        ~CleanupTimerHandler() {
            if (p.timer.is_set(this))
                p.timer.unset(this);
        }
    };
    
    class InactivityTimerHandler : public TimerHandler
    {
        EVSProto& p;
    public:
        InactivityTimerHandler(EVSProto& p_) : TimerHandler("inact"), p(p_) {}
        void handle() 
        {
            Critical crit(p.mon);
            p.check_inactive();
            if (p.timer.is_set(this) == false)
            {
                p.timer.set(this, Period(Time(1, 0)));
            }
        }
        
        ~InactivityTimerHandler() {
            if (p.timer.is_set(this))
                p.timer.unset(this);
        }
    };

    class ConsensusTimerHandler : public TimerHandler
    {
        EVSProto& p;
    public:
        ConsensusTimerHandler(EVSProto& p_) : 
            TimerHandler("consensus"), 
            p(p_)
        {
            
        }
        ~ConsensusTimerHandler()
        {
            if (p.timer.is_set(this))
                p.timer.unset(this);
        }
        
        void handle()
        {
            Critical crit(p.mon);
            
            if (p.get_state() == RECOVERY)
            {
                LOG_WARN("CONSENSUS TIMER");
                p.shift_to(RECOVERY, true);
                if (p.is_consensus() && p.is_representative(p.my_addr))
                {
                    p.send_install();
                }
            }
            else
            {
                LOG_WARN("consensus timer handler in " + to_string(p.get_state()));
            }
        }
    };

    class ResendTimerHandler : public TimerHandler
    {
        EVSProto& p;
    public:
        ResendTimerHandler(EVSProto& p_) :
            TimerHandler("resend"),
            p(p_)
        {
        }
        ~ResendTimerHandler()
        {
            if (p.timer.is_set(this))
                p.timer.unset(this);
        }
        void handle()
        {
            Critical crit(p.mon);
            if (p.get_state() == OPERATIONAL)
            {
                LOG_DEBUG("resend timer handler at " + p.self_string());
                if (p.output.empty())
                {
                    WriteBuf wb(0, 0);
                    p.send_user(&wb, 0xff, DROP, p.send_window, SEQNO_MAX);
                }
                else
                {
                    p.send_user();
                }
                if (p.timer.is_set(this) == false)
                {
                    p.timer.set(this, p.resend_period);
                }
            }
        }
    };

    class SendJoinTimerHandler : public TimerHandler
    {
        EVSProto& p;
    public:
        SendJoinTimerHandler(EVSProto& p_) :
            TimerHandler("send_join"),
            p(p_)
        {
        }
        ~SendJoinTimerHandler()
        {
            if (p.timer.is_set(this))
                p.timer.unset(this);
        }
        void handle()
        {
            Critical crit(p.mon);
            if (p.get_state() == RECOVERY)
            {
                LOG_DEBUG("send join timer handler at " + p.self_string());
                p.send_join(true);
                if (p.timer.is_set(this) == false)
                {
                    p.timer.set(this, p.resend_period);
                }
            }
        }
    };



    InactivityTimerHandler* ith;
    CleanupTimerHandler* cth;
    ConsensusTimerHandler* consth;
    ResendTimerHandler* resendth;
    SendJoinTimerHandler* sjth;
    
    void start_inactivity_timer() 
    {
        timer.set(ith, inactive_check_period);
    }

    void stop_inactivity_timer()
    {
        if (timer.is_set(ith) == false)
        {
            LOG_WARN("inactivity timer is not set, state: " 
                     + to_string(get_state()));
        }
        else
        {
            timer.unset(ith);
        }
    }
    
    void set_consensus_timer()
    {
        timer.set(consth, Period(consensus_timeout));
    }
    
    void unset_consensus_timer()
    {
        if (timer.is_set(consth) == false)
        {
            LOG_DEBUG("consensus timer is not set");
        }
        else
        {
            timer.unset(consth);
        }
    }
    
    bool is_set_consensus_timer()
    {
        return timer.is_set(consth);
    }

    void start_resend_timer()
    {
        timer.set(resendth, resend_period);
    }

    void stop_resend_timer()
    {
        if (timer.is_set(resendth))
        {
            timer.unset(resendth);
        }
    }

    void start_send_join_timer()
    {
        timer.set(sjth, send_join_period);
    }

    void stop_send_join_timer()
    {
        if (timer.is_set(sjth))
        {
            timer.unset(sjth);
        }
    }

    string to_string() const
    {
        // TODO
        std::string v;
        for (std::map<const UUID, EVSInstance>::const_iterator 
                 i = known.begin();
             i != known.end(); ++i)
        {
            v += i->first.to_string() + ":" + i->second.to_string() + " ";
        }
        return "EVS Proto: " + self_string() + ": " + current_view.to_string() + " " + v;
        
    }

};

END_GCOMM_NAMESPACE

#endif // EVS_PROTO_HPP
