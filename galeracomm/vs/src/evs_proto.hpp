#ifndef EVS_PROTO_HPP
#define EVS_PROTO_HPP

#include "evs_input_map.hpp"
#include "evs_message.hpp"
#include "galeracomm/time.hpp"
#include "galeracomm/protolay.hpp"
#include "galeracomm/transport.hpp"

struct EVSProtoUpMeta : public ProtoUpMeta {
    const Address& source;
    EVSProtoUpMeta(const Address& source_) : source(source_) {}
private:
    EVSProtoUpMeta(const EVSProtoUpMeta& evs) :
        ProtoUpMeta(), source(evs.source) {}
    EVSProtoUpMeta& operator= (const EVSProtoUpMeta& evs);
};

struct EVSInstance {
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if instance can be trusted (is reasonably well behaved)
    bool trusted;
    // Known aru map of the instance
    // Commented out, this is needed only on recovery time and 
    // should be found from join message std::map<const EVSPid, uint32_t> aru;
    // Next expected seq from the instance
    // Commented out, this should be found from input map uint32_t expected;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    EVSMessage* join_message;
    // Last activity timestamp
    Time tstamp;
    // CTOR
    EVSInstance() : 
        operational(false), 
        trusted(true), 
        installed(false), join_message(0), 
        tstamp(Time::now()) {}

    ~EVSInstance() {
        delete join_message;
    }

    std::string to_string() const {
        std::string ret;
        ret += "o=";
        ret += (operational ? "1" : "0");
        ret += ",t=";
        ret += (trusted ? "1" : "0");
        ret += ",i=";
        ret += (installed ? "1" : "0");
        return ret;
    }

    EVSInstance (const EVSInstance& i) :
        operational  (i.operational),
        trusted      (i.trusted),
        installed    (i.installed),
        join_message (i.join_message),
        tstamp       (i.tstamp)
    {}

private:

    EVSInstance& operator= (const EVSInstance&);
};




class EVSProto : public Bottomlay {
    EVSProto (const EVSProto&);
    EVSProto& operator= (const EVSProto&);
public:
    Transport* tp;
    EVSProto(Transport* t, const EVSPid& my_addr_) : 
        tp(t),
        my_addr(my_addr_),
        known(),
        inactive_timeout(Time(5, 0)),
        current_view(my_addr, 0),
        input_map(),
        install_message(0),
        installing(false),
        last_delivered_safe(),
        last_sent(SEQNO_MAX),
        send_window(8),
        output(),
        max_output_size(1024),
        self_loopback(false),
        state(CLOSED) {
            std::pair<std::map<const EVSPid, EVSInstance>::iterator, bool> i =
                known.insert(std::pair<const EVSPid, EVSInstance>(my_addr, 
                    EVSInstance()));
            assert(i.second == true);
            i.first->second.operational = true;
            input_map.insert_sa(my_addr);
        }

    ~EVSProto() {
        delete install_message;
    }
    
    EVSPid my_addr;
    typedef std::map<const EVSPid, EVSInstance> InstMap;
    // 
    // Known instances 
    std::map<const EVSPid, EVSInstance> known;
    
    // 
    Time inactive_timeout;
    
    // Current view id
    EVSViewId current_view;
    
    // Map containing received messages and aru/safe seqnos
    EVSInputMap input_map;
    
    // Last received install message
    EVSMessage* install_message;
    // 
    bool installing;
    uint32_t last_delivered_safe;
    
    // Last sent seq
    uint32_t last_sent;
    // Send window size
    uint32_t send_window;
    // Output message queue
    std::deque<WriteBuf*> output;
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
                  const uint32_t);
    int send_user();
    int send_delegate(const EVSPid&, WriteBuf*);
    void send_gap(const EVSPid&, const EVSViewId&, const EVSRange&);
    void send_join();
    void send_leave();
    void send_install();
    
    void resend(const EVSPid&, const EVSGap&);
    void recover(const EVSGap&);

    void do_leave_for(const EVSPid& pid);

    void cleanup_unoperational();

    size_t n_operational() const;

    void deliver();
    void deliver_trans();
    void deliver_reg_view();
    void deliver_trans_view();
    void deliver_empty_view();

    void setall_installed(bool val);
    
    bool is_all_installed() const;
    
    // Compares join message against current state
    
    bool is_consistent(const EVSMessage& jm) const;
    
    bool is_consensus() const;
    
    bool is_representative(const EVSPid& pid) const;

    bool states_compare(const EVSMessage& );
    
    
    void shift_to(const State);
    
    
    // Message handlers
    void handle_notification(const TransportNotification*);
    void handle_user(const EVSMessage&, const EVSPid&, 
                     const ReadBuf*, const size_t);
    void handle_delegate(const EVSMessage&, const EVSPid&, 
                         const ReadBuf*, const size_t);
    void handle_gap(const EVSMessage&, const EVSPid&);
    void handle_join(const EVSMessage&, const EVSPid&);
    void handle_leave(const EVSMessage&, const EVSPid&);
    void handle_install(const EVSMessage&, const EVSPid&);
    
    // Protolay
    int handle_down(WriteBuf* wb, const ProtoDownMeta* dm);
    
    //
    void cleanup() {}
};


#endif // EVS_PROTO_HPP
