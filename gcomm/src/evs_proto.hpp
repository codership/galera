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

#include <set>
#include <list>

using std::set;
using std::list;
using std::deque;
using std::pair;
using std::make_pair;

BEGIN_GCOMM_NAMESPACE

struct EVSInstance {
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if instance can be trusted (is reasonably well behaved)
    bool trusted;
    // Known aru map of the instance
    // Commented out, this is needed only on recovery time and 
    // should be found from join message std::map<const UUID, uint32_t> aru;
    // Next expected seq from the instance
    // Commented out, this should be found from input map uint32_t expected;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    EVSMessage* join_message;
    // Last activity timestamp
    EVSMessage* leave_message;
    // 
    Time tstamp;
    // Human readable name
    string name;
    // CTOR
    EVSInstance(const string& name_) : 
        operational(false), 
        trusted(true), 
        installed(false), 
        join_message(0), 
        leave_message(0),
        tstamp(Time::now()),
        name(name_)
    {
    }

    ~EVSInstance() 
    {
        delete join_message;
        delete leave_message;
    }
    
    void set_name(const string& name)
    {
        this->name = name;
    }

    const string& get_name() const
    {
        return name;
    }

    string to_string() const {
        std::string ret;
        ret += "'" + name + "':";
        ret += "o=";
        ret += (operational ? "1" : "0");
        ret += ",t=";
        ret += (trusted ? "1" : "0");
        ret += ",i=";
        ret += (installed ? "1" : "0");
        return ret;
    }

    void update_tstamp() {
        tstamp = Time::now();
    }

};


#define SHIFT_TO(_s) do                                         \
    {                                                           \
        LOG_DEBUG(string(__FILE__) + ":" + __FUNCTION__ + ":" + Int(__LINE__).to_string()); \
        shift_to(_s);                                           \
    }                                                           \
    while (0)

#define SHIFT_TO_P(_p, _s) do                                     \
    {                                                           \
        LOG_DEBUG(string(__FILE__) + ":" + __FUNCTION__ + ":" + Int(__LINE__).to_string()); \
        (_p)->shift_to(_s);                                            \
    }                                                           \
    while (0)



class EVSProto : public Protolay
{
public:
    Monitor mon;
    Transport* tp;
    EVSProto(EventLoop* poll_, Transport* t, const UUID& my_addr_, 
             const string& name) : 
        tp(t),
        my_addr(my_addr_), 
        my_name(name),
        inactive_timeout(Time(1, 0)),
        consensus_timeout(Time(1, 0)),
        resend_timeout(Time(1, 0)),
        timer(poll_),
        current_view(View::V_TRANS, ViewId(my_addr, 0)),
        install_message(0),
        installing(false),
        last_sent(SEQNO_MAX),
        send_window(8), 
        max_output_size(1024),
        self_loopback(false),
        state(CLOSED) 
    {
        LOG_DEBUG("EVSProto(): (" + my_addr.to_string() + "," + my_name + ")");
        pair<std::map<const UUID, EVSInstance>::iterator, bool> i =
            known.insert(make_pair(my_addr, EVSInstance(my_name)));
        assert(i.second == true);
        i.first->second.operational = true;
        input_map.insert_sa(my_addr);
        current_view.add_member(my_addr, my_name);
        ith = new InactivityTimerHandler(this);
        cth = new CleanupTimerHandler(this);
        consth = new ConsensusTimerHandler(this);
        resendth = new ResendTimerHandler(this);
    }
    
    ~EVSProto() {
        for (deque<WriteBuf*>::iterator i = output.begin(); i != output.end();
             ++i)
        {
            delete *i;
        }
        output.clear();
        delete install_message;
        delete ith;
        delete cth;
        delete consth;
        delete resendth;
    }
    
    UUID my_addr;
    string my_name;

    const UUID& get_uuid() const
    {
        return my_addr;
    }

    string self_string() const
    {
        return "(" + my_addr.to_string() + "," + my_name + ")";
    }
    
    typedef std::map<const UUID, EVSInstance> InstMap;
    const UUID& get_pid(InstMap::const_iterator i) const
    {
        return i->first;
    }
    const EVSInstance& get_instance(InstMap::const_iterator i) const
    {
        return i->second;
    }
    // 
    // Known instances 
    InstMap known;
    
    // 
    Time inactive_timeout;
    Time consensus_timeout;
    Time resend_timeout;
    Timer timer;
    
    // Current view id
    // ViewId current_view;
    View current_view;
    list<pair<View, Time> > previous_views;
    
    // Map containing received messages and aru/safe seqnos
    EVSInputMap input_map;
    
    // Last received install message
    EVSMessage* install_message;
    // 
    bool installing;
    // Last sent seq
    uint32_t last_sent;
    // Send window size
    uint32_t send_window;
    // Output message queue
    deque<WriteBuf*> output;
    uint32_t max_output_size;


    bool self_loopback;
    
    enum State {
        CLOSED,
        JOINING,
        LEAVING,
        RECOVERY, 
        OPERATIONAL,
        STATE_MAX
    };
    State state;
    
    State get_state() const {
        return state;
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
    int send_user(WriteBuf* wb, EVSSafetyPrefix sp, 
                  const uint32_t, 
                  const uint32_t, bool local = false);
    int send_user();
    int send_delegate(const UUID&, WriteBuf*);
    void send_gap(const UUID&, const ViewId&, const EVSRange&);
    EVSJoinMessage create_join() const;
    void send_join(bool);
    void send_join()
    {
        send_join(true);
    }
    void set_join(const EVSMessage&, const UUID&);
    void set_leave(const EVSMessage&, const UUID&);
    void send_leave();
    void send_install();
    
    void resend(const UUID&, const EVSGap&);
    void recover(const EVSGap&);

    void check_inactive();
    void cleanup_unoperational();
    void cleanup_views();

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
    
    
    void shift_to(const State);
    
    
    // Message handlers
    void handle_user(const EVSMessage&, const UUID&, 
                     const ReadBuf*, const size_t);
    void handle_delegate(const EVSMessage&, const UUID&, 
                         const ReadBuf*, const size_t);
    void handle_gap(const EVSMessage&, const UUID&);
    void handle_join(const EVSMessage&, const UUID&);
    void handle_leave(const EVSMessage&, const UUID&);
    void handle_install(const EVSMessage&, const UUID&);

    void handle_msg(const EVSMessage& msg, const UUID& source,
                          const ReadBuf* rb, const size_t roff);    
    // Protolay
    void handle_up(const int, const ReadBuf*, const size_t, const ProtoUpMeta*);
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm);
    
    //
    void cleanup() {}



    class CleanupTimerHandler : public TimerHandler
    {
        EVSProto* p;
    public:
        CleanupTimerHandler(EVSProto* p_) : TimerHandler("cleanup"), p(p_) {}
        void handle() 
        {
            Critical crit(&p->mon);
            p->cleanup_unoperational();
        }
        ~CleanupTimerHandler() {
            if (p->timer.is_set(this))
                p->timer.unset(this);
        }
    };
    
    class InactivityTimerHandler : public TimerHandler
    {
        EVSProto* p;
    public:
        InactivityTimerHandler(EVSProto* p_) : TimerHandler("inact"), p(p_) {}
        void handle() 
        {
            Critical crit(&p->mon);
            p->check_inactive();
        }
        
        ~InactivityTimerHandler() {
            if (p->timer.is_set(this))
                p->timer.unset(this);
        }
    };

    class ConsensusTimerHandler : public TimerHandler
    {
        EVSProto* p;
    public:
        ConsensusTimerHandler(EVSProto* p_) : 
            TimerHandler("consensus"), 
            p(p_)
        {
            
        }
        ~ConsensusTimerHandler()
        {
            if (p->timer.is_set(this))
                p->timer.unset(this);
        }

        void handle()
        {
            Critical crit(&p->mon);
            LOG_DEBUG("CONSENSUS TIMER");
            if (p->get_state() == RECOVERY)
            {
                SHIFT_TO_P(p, RECOVERY);
                p->send_join();
            }
            else
            {
                LOG_WARN("consensus timer handler in " + to_string(p->get_state()));
            }
        }
    };

    class ResendTimerHandler : public TimerHandler
    {
        EVSProto* p;
    public:
        ResendTimerHandler(EVSProto* p_) :
            TimerHandler("resend"),
            p(p_)
        {
        }
        ~ResendTimerHandler()
        {
            if (p->timer.is_set(this))
                p->timer.unset(this);
        }
        void handle()
        {
            Critical crit(&p->mon);
            if (p->get_state() == OPERATIONAL)
            {
                LOG_DEBUG("resend timer handler at " + p->self_string());
                if (p->output.empty())
                {
                    WriteBuf wb(0, 0);
                    p->send_user(&wb, DROP, p->send_window, SEQNO_MAX);
                }
                else
                {
                    p->send_user();
                }
                p->timer.set(this, Period(p->resend_timeout));
            }
        }
    };

    InactivityTimerHandler* ith;
    CleanupTimerHandler* cth;
    ConsensusTimerHandler* consth;
    ResendTimerHandler* resendth;
    
    void set_inactivity_timer() {
        timer.set(ith, Period(inactive_timeout));
    }

    void set_consensus_timer()
    {
        timer.set(consth, Period(consensus_timeout));
    }

    void unset_consensus_timer()
    {
        if (timer.is_set(consth))
        {
            LOG_DEBUG("consensus timer is not set");
        }
        timer.unset(consth);
    }
    
    bool is_set_consensus_timer()
    {
        return timer.is_set(consth);
    }

    void start_resend_timer()
    {
        timer.set(resendth, Period(resend_timeout));
    }

    void stop_resend_timer()
    {
        if (timer.is_set(resendth))
        {
            timer.unset(resendth);
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
