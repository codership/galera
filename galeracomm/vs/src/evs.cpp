
// EVS backend implementation based on TIPC 

//
// State RECOVERY:
// Input:
//   USER - If message from the current view then queue, update aru 
//          and expected 
//          Output: If from current view, aru changed and flow control
//                  allows send GAP message
//                  Else if source is not known, add to set of known nodes
//   GAP  - If INSTALL received update aru and expected
//          Else if GAP message matches to INSTALL message, add source
//               to install_acked
//          Output: If all in install_acked = true, 
//   JOIN - add to join messages, add source to install acked with false 
//          status, compute consensus
//          Output: 
//          If consensus reached and representative send INSTALL
//          If state was updated, send JOIN
//   INSTALL - Output:
//          If message state matches to current send GAP message
//          Else send JOIN message
//
// INSTALL message carries 
// - seqno 0
// - UUID for the new view
// - low_aru and high_aru
// - 
// 
//
//
//

#include "evs.hpp"
#include "evs_input_map.hpp"

#include "gcomm/time.hpp"

struct EVSInstance {
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    //
    bool leaving;
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
        leaving(false),
        trusted(true), 
        installed(false), join_message(0), tstamp(Time::now()) {}
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

};




class EVSProto : public Bottomlay {
public:
    Transport* tp;
    EVSProto(Transport* t, const EVSPid& my_addr_) : 
        tp(t),
        my_addr(my_addr_), 
        inactive_timeout(Time(5, 0)),
        current_view(my_addr, 0),
        install_message(0),
        installing(false),
        last_sent(SEQNO_MAX),
        send_window(8), 
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
    void deliver();
    void deliver_trans();
    void deliver_reg_view() {
    // TODO
        std::string v;
        for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin();
             i != known.end(); ++i)
            v += i->first.to_string() + ":" + i->second.to_string() + " ";
        LOG_INFO("Reg view on " + my_addr.to_string() + ": " + current_view.to_string() + " -> " + install_message->get_source_view().to_string() + " " + v);
    }
    void deliver_trans_view() {
    // TODO
        std::string v;
        for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin();
             i != known.end(); ++i)
            v += i->first.to_string() + ":" + i->second.to_string() + " ";
        LOG_INFO("Trans view on " + my_addr.to_string() + ": " + current_view.to_string() + " -> " + install_message->get_source_view().to_string() + " " + v);
    }
    
    void deliver_empty_view() {
        // TODO
        LOG_INFO("Empty view on " + my_addr.to_string());
    }

    void setall_installed(bool val) {
        for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin();
             i != known.end(); ++i) {
                 i->second.installed = val;
             }
    }
    
    bool is_all_installed() const {
        std::string v;
        for (std::map<const EVSPid, EVSInstance>::const_iterator i = known.begin();
             i != known.end(); ++i)
            v += i->first.to_string() + ":" + i->second.to_string() + " ";
        LOG_DEBUG(my_addr.to_string() + ": " + current_view.to_string() + " -> " + install_message->get_source_view().to_string() + " " + v);
        for (std::map<const EVSPid, EVSInstance>::const_iterator i =
                 known.begin();
             i != known.end(); ++i) {
            if (i->second.operational && 
                i->second.trusted && 
                i->second.installed == false)
                return false;
        }
        return true;
    }
    
    // Compares join message against current state
    
    bool is_consistent(const EVSMessage& jm) const;
    
    bool is_consensus() const {
        const EVSMessage* my_jm = known.find(my_addr)->second.join_message;
        if (my_jm == 0) {
            LOG_DEBUG("is_consensus(): no own join message");
            return false;
        }
        if (is_consistent(*my_jm) == false) {
            LOG_DEBUG("is_consistent(): own join message is not consistent");
            return false;
        }
        
        for (std::map<const EVSPid, EVSInstance>::const_iterator 
             i = known.begin(); i != known.end(); ++i) {
                 if (!(i->second.operational && i->second.trusted))
                     continue;
                 if (i->second.join_message == 0)
                     return false;
                 if (is_consistent(*i->second.join_message) == false)
                     return false;
        }
        return true;
    }
    
    bool is_representative(const EVSPid& pid) const {
        for (std::map<const EVSPid, EVSInstance>::const_iterator i =
known.begin();
             i != known.end(); ++i) {
                 if (i->second.operational && i->second.trusted) {
                     if (pid == i->first)
                         return true;
                     else
                         return false;
                 }
             }
        return false;
    }

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


bool EVSProto::is_consistent(const EVSMessage& jm) const
{
    std::map<const EVSPid, EVSRange> local_insts;
    std::map<const EVSPid, EVSRange> jm_insts;


    if (jm.get_source_view() == current_view) {
        // Compare instances that originate from the current view and 
        // should proceed to next view

        // First check agains input map state
        if (!(seqno_eq(input_map.get_aru_seq(), jm.get_aru_seq()) &&
              seqno_eq(input_map.get_safe_seq(), jm.get_seq()))) {
            return false;
        }
        
        for (std::map<const EVSPid, EVSInstance>::const_iterator i =
                 known.begin();
             i != known.end(); ++i) {
            if (i->second.operational == true && 
                i->second.trusted == true &&
                i->second.join_message && 
                i->second.join_message->get_source_view() == current_view)
                local_insts.insert(std::pair<const EVSPid, EVSRange>(
                                       i->first, input_map.get_sa_gap(i->first)));
        }
        const std::map<EVSPid, EVSMessage::Instance>* jm_instances = 
            jm.get_instances();
        for (std::map<EVSPid, EVSMessage::Instance>::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) {
            if (i->second.get_operational() == true && 
                i->second.get_trusted() == true &&
                i->second.get_view_id() == current_view) {
                jm_insts.insert(
                    std::pair<const EVSPid, EVSRange>(
                        i->second.get_pid(),
                        i->second.get_range()));
            } 
        }
        if (jm_insts != local_insts)
            return false;
        jm_insts.clear();
        local_insts.clear();
        
        // Compare instances that originate from the current view but 
        // are not going to proceed to next view
        
        for (std::map<const EVSPid, EVSInstance>::const_iterator 
                 i = known.begin(); i != known.end(); ++i)
        {
            if (!(i->second.operational == true && i->second.trusted == true))
            {
                local_insts.insert(std::pair<const EVSPid, EVSRange>(
                                       i->first, input_map.contains_sa(i->first) ?
                                       input_map.get_sa_gap(i->first) : EVSRange()));
            }
            const std::map<EVSPid, EVSMessage::Instance>* jm_instances =
                jm.get_instances();
            for (std::map<EVSPid, EVSMessage::Instance>::const_iterator
                     i = jm_instances->begin(); i != jm_instances->end(); ++i)
            {
                if (!(i->second.get_operational() == true &&
                      i->second.get_trusted() == true))
                {
                    jm_insts.insert(std::pair<const EVSPid,
                                    EVSRange>(i->second.get_pid(),
                                              i->second.get_range()));
                }
            }
        }
        if (jm_insts != local_insts)
            return false;
        jm_insts.clear();
        local_insts.clear();
    } else {
        // Instances are originating from different view, need to check
        // only that new view is consistent
        for (std::map<const EVSPid, EVSInstance>::const_iterator i =
                 known.begin();
             i != known.end(); ++i) {
            if (i->second.operational == true && i->second.trusted == true
                &&
                i->second.join_message)
                local_insts.insert(std::pair<const EVSPid, EVSRange>(
                                       i->first, EVSRange()));
        }
        const std::map<EVSPid, EVSMessage::Instance>* jm_instances = 
            jm.get_instances();
        for (std::map<EVSPid, EVSMessage::Instance>::const_iterator 
                 i = jm_instances->begin(); i != jm_instances->end(); ++i) {
            if (i->second.get_operational() == true && 
                i->second.get_trusted() == true) {
                jm_insts.insert(
                    std::pair<const EVSPid, EVSRange>(
                        i->second.get_pid(), EVSRange()));
            } 
        }
        if (jm_insts != local_insts)
            return false;
        jm_insts.clear();
        local_insts.clear();
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////

bool EVSProto::is_flow_control(const uint32_t seq, const uint32_t win) const
{
    // Flow control
    if (!seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) && 
        seqno_lt(seqno_add(input_map.get_aru_seq(), win), seq)) {
        // LOG_INFO("flow control");
        return true;
    } else if (seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) && 
               seqno_lt(win, seq)) {
        return true;
    }
    return false;
}

int EVSProto::send_user(WriteBuf* wb, const EVSSafetyPrefix sp, 
                        const uint32_t win,
                        const uint32_t up_to_seqno)
{
    assert(get_state() == LEAVING || get_state() == RECOVERY || get_state() == OPERATIONAL);
    int ret;
    uint32_t seq = seqno_eq(last_sent, SEQNO_MAX) ? 0 : seqno_next(last_sent);
    
    if (is_flow_control(seq, win))
        return EAGAIN;
    
    uint32_t seq_range = seqno_eq(up_to_seqno, SEQNO_MAX) ? 0 :
        seqno_dec(up_to_seqno, seq);
    assert(seq_range < 0x100U);
    uint32_t last_msg_seq = seqno_add(seq, seq_range);
    
    uint8_t flags;
    
    // LOG_INFO("output size: " + ::to_string(output.size()) 
    // + " aru_seq: " + ::to_string(input_map.get_aru_seq()) 
    // + " last_msg_seq: " + ::to_string(last_msg_seq));
    if (output.size() < 2 || 
        !seqno_eq(up_to_seqno, SEQNO_MAX) ||
        is_flow_control(seqno_next(last_msg_seq), win)) {
        flags = 0;
    } else {
        LOG_DEBUG("msg more");
        flags = EVSMessage::F_MSG_MORE;
    }
    
    EVSMessage msg(EVSMessage::USER, my_addr, sp, seq, seq_range & 0xffU, 
                   input_map.get_aru_seq(), current_view, 
                   flags);
    
    wb->prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
    if ((ret = tp->handle_down(wb, 0)) == 0) {
        last_sent = last_msg_seq;

        ReadBuf* rb = wb->to_readbuf();
        EVSRange range = input_map.insert(EVSInputMapItem(my_addr, msg, rb, 0));
        assert(seqno_eq(range.get_high(), last_sent));
        input_map.set_safe(my_addr, last_sent);
        rb->release();
    }
    wb->rollback_hdr(msg.get_hdrlen());
    return ret;
}

int EVSProto::send_user()
{
    
    if (output.empty())
        return 0;
    assert(state == OPERATIONAL);
    WriteBuf* wb = output.front();
    int ret;
    if ((ret = send_user(wb, SAFE, send_window, SEQNO_MAX)) == 0) {
        output.pop_front();
        delete wb;
    }
    return ret;
}

int EVSProto::send_delegate(const EVSPid& sa, WriteBuf* wb)
{
    EVSMessage dm(EVSMessage::DELEGATE, sa);
    wb->prepend_hdr(dm.get_hdr(), dm.get_hdrlen());
    return tp->handle_down(wb, 0);
}

void EVSProto::send_gap(const EVSPid& pid, const EVSViewId& source_view, 
                        const EVSRange& range)
{
    LOG_DEBUG("send gap at " + my_addr.to_string() + " to "  + pid.to_string() + " requesting range " + range.to_string());
    // TODO: Investigate if gap sending can be somehow limited, 
    // message loss happen most probably during congestion and 
    // flooding network with gap messages won't probably make 
    // conditions better
    EVSGap gap(pid, range);
    EVSMessage gm(EVSMessage::GAP, my_addr, source_view, last_sent, input_map.get_aru_seq(), gap);
    
    size_t bufsize = gm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (gm.write(buf, bufsize, 0) == 0)
        throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = tp->handle_down(&wb, 0))) {
        LOG_WARN(std::string("send failed ") 
                 + strerror(err));
    }
    delete[] buf;
}


void EVSProto::send_join()
{
    LOG_DEBUG("send join at " + my_addr.to_string());
    EVSMessage jm(EVSMessage::JOIN, 
                  my_addr,
                  current_view, 
                  input_map.get_aru_seq(), input_map.get_safe_seq());
    for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin();
         i != known.end(); ++i) {
             jm.add_instance(i->first, i->second.operational, i->second.trusted,
                             i->second.join_message ? 
                             i->second.join_message->get_source_view() : 
                             (input_map.contains_sa(i->first) ? 
                              current_view : EVSViewId()), 
                             input_map.contains_sa(i->first) ? 
                             input_map.get_sa_gap(i->first) : EVSRange());
         }
    size_t bufsize = jm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (jm.write(buf, bufsize, 0) == 0)
        throw FatalException("failed to serialize join message");
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = tp->handle_down(&wb, 0))) {
        LOG_WARN(std::string("EVSProto::send_join(): Send failed ") 
                 + strerror(err));
    }
    delete[] buf;
}

void EVSProto::send_leave()
{
    EVSMessage lm(EVSMessage::LEAVE, my_addr, current_view);
    size_t bufsize = lm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (lm.write(buf, bufsize, 0) == 0)
        throw FatalException("failed to serialize leave message");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = tp->handle_down(&wb, 0))) {
        LOG_WARN(std::string("EVSProto::send_leave(): Send failed ") 
                 + strerror(err));
    }
    delete[] buf;
}

void EVSProto::send_install()
{
    LOG_DEBUG("sending install at " + my_addr.to_string() + " installing flag " + ::to_string(installing));
    if (installing)
        return;
    std::map<const EVSPid, EVSInstance>::iterator self = known.find(my_addr);
    EVSMessage im(EVSMessage::INSTALL, 
                  my_addr,
                  EVSViewId(my_addr, current_view.get_seq() + 1),
                  input_map.get_aru_seq(), input_map.get_safe_seq());
    for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin();
         i != known.end(); ++i) {
             im.add_instance(i->first, 
                             i->second.operational,
                             i->second.trusted,
                             i->second.join_message ? 
                             i->second.join_message->get_source_view() : 
                             (input_map.contains_sa(i->first) ? 
                              current_view : EVSViewId()), 
                             input_map.contains_sa(i->first) ? 
                             input_map.get_sa_gap(i->first) : EVSRange());
         }
    size_t bufsize = im.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (im.write(buf, bufsize, 0) == 0)
        throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = tp->handle_down(&wb, 0))) {
        LOG_WARN(std::string("send failed ") 
                 + strerror(err));
    }
    delete[] buf;
    installing = true;
}


void EVSProto::resend(const EVSPid& gap_source, const EVSGap& gap)
{
    assert(gap.source == my_addr);
    if (seqno_eq(gap.get_high(), SEQNO_MAX)) {
        LOG_DEBUG(std::string("empty gap") 
                  + ::to_string(gap.get_low()) + " -> " 
                  + ::to_string(gap.get_high()));
        return;
    } else if (!seqno_eq(gap.get_low(), SEQNO_MAX) &&
               seqno_gt(gap.get_low(), gap.get_high())) {
        LOG_DEBUG(std::string("empty gap") 
                  + ::to_string(gap.get_low()) + " -> " 
                  + ::to_string(gap.get_high()));
        return;
    }
    
    uint32_t start_seq = seqno_eq(gap.get_low(), SEQNO_MAX) ? 0 : gap.get_low();
    LOG_INFO("resending at " + my_addr.to_string() + " requested by " + gap_source.to_string() + " " + ::to_string(start_seq) + " -> " + ::to_string(gap.get_high()));

    for (uint32_t seq = start_seq; !seqno_gt(seq, gap.get_high()); ) {
        std::pair<EVSInputMapItem, bool> i = input_map.recover(my_addr,
                                                               seq);
        if (i.second == false) {
            std::map<const EVSPid, EVSInstance>::iterator ii =
                known.find(gap_source);
            assert(ii != known.end());
            ii->second.trusted = false;
            shift_to(RECOVERY);
            return;
        } else {
            const ReadBuf* rb = i.first.get_readbuf();
            const EVSMessage& msg = i.first.get_evs_message();
            assert(seqno_eq(msg.get_seq(), seq));
            EVSMessage new_msg(msg.get_type(), 
                               msg.get_source(), 
                               msg.get_safety_prefix(),
                               msg.get_seq(), 
                               msg.get_seq_range(),
                               input_map.get_aru_seq(),
                               msg.get_source_view(), 
                               EVSMessage::F_RESEND);
            WriteBuf wb(rb ? rb->get_buf() : 0, rb ? rb->get_len() : 0);
            wb.prepend_hdr(new_msg.get_hdr(), new_msg.get_hdrlen());
            if (tp->handle_down(&wb, 0))
                break;
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        }
    }
}

void EVSProto::recover(const EVSGap& gap)
{
    
    if (gap.get_low() == gap.get_high()) {
        LOG_WARN(std::string("EVSProto::recover(): Empty gap: ") 
                 + ::to_string(gap.get_low()) + " -> " 
                 + ::to_string(gap.get_high()));
        return;
    }
    
    // TODO: Find out a way to select only single instance that
    // is allowed to recover messages
    for (uint32_t seq = gap.get_low(); !seqno_gt(seq, gap.get_high()); ) {
        std::pair<EVSInputMapItem, bool> i = input_map.recover(gap.source,
                                                               seq);
        if (i.second == true) {
            const ReadBuf* rb = i.first.get_readbuf();
            const EVSMessage& msg = i.first.get_evs_message();
            WriteBuf wb(rb, rb ? rb->get_len() : 0);
            wb.prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
            if (send_delegate(gap.source, &wb))
                break;
            seq = seqno_add(seq, msg.get_seq_range() + 1);
        } else {
            seq = seqno_next(seq);
        }
    }
}

////////////////////////////////////////////////////////////////////////
// Protolay interface
////////////////////////////////////////////////////////////////////////

int EVSProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    if (get_state() != RECOVERY && get_state() != OPERATIONAL)
        return ENOTCONN;
    
    int ret = 0;

    if (output.empty() && get_state() == OPERATIONAL) {
        int err = send_user(wb, SAFE, send_window/2, SEQNO_MAX);
        switch (err) {
        case EAGAIN: {
            WriteBuf* priv_wb = wb->copy();
            output.push_back(priv_wb);
        }
            // Fall through
        case 0:
            break;
        default:
            LOG_ERROR(std::string("Send error: ") + ::to_string(err));
            ret = err;
        }
    } else if (output.size() < max_output_size) {
        WriteBuf* priv_wb = wb->copy();
        output.push_back(priv_wb);
    } else {
        LOG_WARN("Output queue full");
        ret = EAGAIN;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// State handler
/////////////////////////////////////////////////////////////////////////////

void EVSProto::shift_to(const State s)
{
    static const bool allowed[STATE_MAX][STATE_MAX] = {
    // CLOSED
        {false, true, false, false, false},
    // JOINING
        {false, false, true, true, false},
    // LEAVING
        {true, false, false, false, false},
    // RECOVERY
        {false, false, true, true, true},
    // OPERATIONAL
        {false, false, true, true, false}
    };
    
    assert(s < STATE_MAX);
    if (allowed[state][s] == false) {
        LOG_FATAL(std::string("invalid state transition: ") 
                  + to_string(state) + " -> " + to_string(s));
        throw FatalException("invalid state transition");
    }
    
    LOG_INFO(std::string("state change: ") + 
             to_string(state) + " -> " + to_string(s));
    
    switch (s) {
    case CLOSED:
        cleanup();
        state = CLOSED;
        break;
    case JOINING:
        state = JOINING;
        break;
    case LEAVING:
        send_leave();
        state = LEAVING;
        break;
    case RECOVERY:
        setall_installed(false);
        delete install_message;
        install_message = 0;
        installing = false;
        state = RECOVERY;
        break;
    case OPERATIONAL:
        assert(is_consensus() == true);
        assert(is_all_installed() == true);
        deliver();
        deliver_trans_view();
        deliver_trans();
        deliver_reg_view();
    // Reset input map
        input_map.clear();
        for (std::map<const EVSPid, EVSInstance>::iterator i = known.begin(); i
!= known.end(); ++i)
            if (i->second.installed)
                input_map.insert_sa(i->first);
        current_view = install_message->get_source_view();
        last_sent = SEQNO_MAX;
        state = OPERATIONAL;
        break;
    default:
        throw FatalException("Invalid state");
    }
}

////////////////////////////////////////////////////////////////////////////
// Message delivery
////////////////////////////////////////////////////////////////////////////

void EVSProto::deliver()
{
    if (state != OPERATIONAL && state != RECOVERY && get_state() != LEAVING)
        throw FatalException("Invalid state");
    
    LOG_DEBUG("aru_seq: " + ::to_string(input_map.get_aru_seq()) + " safe_seq: "
              + ::to_string(input_map.get_safe_seq()));
    
    EVSInputMap::iterator i, i_next;
    // First deliver all messages that qualify at least as safe
    for (i = input_map.begin();
         i != input_map.end() && input_map.is_safe(i); i = i_next) {
        i_next = i;
        ++i_next;
        assert(i->get_evs_message().get_source_view() == current_view);
        if (i->get_evs_message().get_safety_prefix() != DROP) {
            EVSProtoUpMeta um(i->get_sockaddr());
            pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        }
        input_map.erase(i);
    }
    // Deliver all messages that qualify as agreed
    for (; i != input_map.end() && 
             i->get_evs_message().get_safety_prefix() == AGREED &&
             input_map.is_agreed(i); i = i_next) {
        i_next = i;
        ++i_next;
        assert(i->get_evs_message().get_source_view() == current_view);
        EVSProtoUpMeta um(i->get_sockaddr());
        pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        input_map.erase(i);
    }
    // And finally FIFO or less 
    for (; i != input_map.end() &&
             i->get_evs_message().get_safety_prefix() == FIFO &&
             input_map.is_fifo(i); i = i_next) {
        i_next = i;
        ++i_next;
        assert(i->get_evs_message().get_source_view() == current_view);
        EVSProtoUpMeta um(i->get_sockaddr());
        pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
        input_map.erase(i);
    }
}

void EVSProto::deliver_trans()
{
    if (state != RECOVERY)
        throw FatalException("Invalid state");
    // In transitional configuration we must deliver all messages that 
    // are fifo. This is because:
    // - We know that it is possible to deliver all fifo messages originated
    //   from partitioned component as safe in partitioned component
    // - Aru in this component is at least the max known fifo seq
    //   from partitioned component due to message recovery 
    // - All FIFO messages originated from this component must be 
    //   delivered to fulfill self delivery requirement and
    // - FIFO messages originated from this component qualify as AGREED
    //   in transitional configuration
    
    EVSInputMap::iterator i, i_next;
    for (i = input_map.begin(); i != input_map.end() && 
         input_map.is_agreed(i); i = i_next) {
             i_next = i;
             ++i_next;
             assert(i->get_evs_message().get_source_view() == current_view);
             if (i->get_evs_message().get_safety_prefix() != DROP) {
                 EVSProtoUpMeta um(i->get_sockaddr());
                 pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
             }
             input_map.erase(i);
         }
    
    for (; i != input_map.end() &&
         input_map.is_fifo(i); i = i_next) {
             i_next = i;
             ++i_next;
             assert(known.find(i->get_sockaddr()) != known.end() && 
                    known.find(i->get_sockaddr())->second.installed); 
             if (i->get_evs_message().get_safety_prefix() != DROP) {
                 EVSProtoUpMeta um(i->get_sockaddr());
                 pass_up(i->get_readbuf(), i->get_payload_offset(), &um);
             }
             input_map.erase(i);
         }
    
    // Sanity check:
    // There must not be any messages left that 
    // - Are originated from outside of trans conf and are FIFO
    // - Are originated from trans conf
    for (i = input_map.begin(); i != input_map.end(); i = i_next) {    
        i_next = i;
        ++i_next;
        std::map<const EVSPid, EVSInstance>::iterator ii =
known.find(i->get_sockaddr());
        if (ii->second.installed)
            throw FatalException("Protocol error in transitional delivery "
                                 "(self delivery constraint)");
        else if (input_map.is_fifo(i))
            throw FatalException("Protocol error in transitional delivery "
                                 "(fifo from partitioned component)");
        input_map.erase(i);
    }
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////

void EVSProto::handle_notification(const TransportNotification *tn)
{
#if 0
    std::map<EVSPid, EVSInstance>::iterator i = known.find(tn->source_sa);
    
    if (i == known.end() && tn->ntype == TRANSPORT_N_SUBSCRIBED) {
    known.insert(std::pair<EVSPid, EVSInstance >(source, EVSInstance()));
    } else if (i != known.end() && tn->ntype == TRANSPORT_N_WITHDRAWN) {
    if (i->second->operational == true) {
        // Instance was operational but now it has withdrawn, 
        // mark it as unoperational.
        i->second->operational = false;
        shift_to(RECOVERY);
    } 
    } else if (tn->ntype == TRANSPORT_N_SUBSCRIBED) {
    LOG_WARN("Double subscription");
    } else if (tn->ntype == TRANSPORT_N_WITHDRAWN) {
    LOG_WARN("Unknown withdrawn");
    } else if (tn->ntype == TRANSPORT_N_FAILURE) {
    // We must exit now
    // TODO: Do it a bit more gently
    throw FatalException("Transport lost");
    } else {
        LOG_WARN("Unhandled transport notification: " + to_string(tn->ntype));
    }
    if (my_addr != ADDRESS_INVALID || state == JOINING)
    send_join();
#endif // 0
}

void EVSProto::handle_user(const EVSMessage& msg, const EVSPid& source, 
                           const ReadBuf* rb, const size_t roff)
{
    LOG_DEBUG("this: " + my_addr.to_string() + " source: "
              + source.to_string() + " seq: " + ::to_string(msg.get_seq()));
    std::map<const EVSPid, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
        // Previously unknown instance has appeared and it seems to
        // be operational, assume that it can be trusted and start
        // merge/recovery
        LOG_INFO("new instance");
        std::pair<std::map<const EVSPid, EVSInstance>::iterator, bool> iret;
        iret = known.insert(std::pair<EVSPid, EVSInstance>(source,
                                                           EVSInstance()));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) {
            shift_to(RECOVERY);
            send_join();
        }
        return;
    } else if (state == JOINING || state == CLOSED) {
        // Drop message
        LOG_DEBUG("dropping");
        return;
    } else if (!(msg.get_source_view() == current_view)) {
        if (i->second.trusted == false) {
            LOG_DEBUG("untrusted source");
            // Do nothing, just discard message
            return;
        } else if (i->second.operational == false) {
            // This is probably partition merge, see if it works out
            LOG_DEBUG("unoperational source");
            i->second.operational = true;
            shift_to(RECOVERY);
            send_join();
            return;
        } else if (i->second.installed == false) {
            if (install_message && 
                msg.get_source_view() == install_message->get_source_view()) {
                assert(state == RECOVERY);
                LOG_DEBUG("recovery user message source");
                // Other instances installed view before this one, so it is 
                // safe to shift to OPERATIONAL if consensus has been reached
                if (is_consensus()) {
                    shift_to(OPERATIONAL);
                } else {
                    shift_to(RECOVERY);
                    send_join();
                    return;
                }
            } else {
                // Probably caused by network partitioning during recovery
                // state, this will most probably lead to view 
                // partition/remerge. In order to do it in organized fashion,
                // don't trust the source instance during recovery phase.
                // LOG_WARN("Setting " + source.to_string() + " status to no-trust" + " on " + my_addr.to_string());
                // i->second.trusted = false;
                // Note: setting other instance to non-trust here is too harsh
                // shift_to(RECOVERY);
                // send_join();
                return;
            }
        } else {
            i->second.trusted = false;
            shift_to(RECOVERY);
            send_join();
            return;
        }
    }
    
    assert(i->second.trusted == true && 
           (i->second.operational == true || i->second.leaving == true) &&
           (i->second.installed == true || get_state() == RECOVERY) &&
           msg.get_source_view() == current_view);
    
    if (source == my_addr) {
        // own messages must already exist in input_map
        return;
    }


    uint32_t prev_aru = input_map.get_aru_seq();
    EVSRange range(input_map.insert(EVSInputMapItem(source, msg, rb, roff)));
    
    // Some messages are missing
    // 
    // TODO: 
    // - Gap messages should take list of gaps to avoid sending gap 
    //   message for each missing message
    // - There should be guard (timer etc) to avoid sending gap message
    //   for each incoming packet from the source of missing packet 
    //   (maybe this is not too bad if gap message contains gap list
    //   and message loss is infrequent)
    if ((seqno_eq(range.get_low(), SEQNO_MAX) ||
         seqno_gt(range.get_high(), range.get_low())) &&
        !(msg.get_flags() & EVSMessage::F_RESEND)) {
        LOG_INFO("requesting at " + my_addr.to_string() + " from " + source.to_string() + " " + range.to_string() + " due to input map gap, aru " + ::to_string(input_map.get_aru_seq()));
#if 0
        std::list<EVSRange> gap_list = input_map.get_gap_list(source);
        for (std::list<EVSRange>::iterator gi = gap_list.begin();
             gi != gap_list.end(); ++gi) {
            LOG_INFO(gi->to_string());
            send_gap(source, current_view, *gi);
        }
#else
        send_gap(source, current_view, range);
#endif
    }
    
    if (!(i->first == my_addr) && 
        ((output.empty() && !(msg.get_flags() & EVSMessage::F_MSG_MORE)) ||
         state == RECOVERY) && 
        /* !seqno_eq(range.get_high(), SEQNO_MAX) && */
        (seqno_eq(last_sent, SEQNO_MAX) || 
         seqno_lt(last_sent, range.get_high()))) {
        // Message not originated from this instance, output queue is empty
        // and last_sent seqno should be advanced
        LOG_DEBUG("sending dummy: " + ::to_string(last_sent) + " -> " + ::to_string(range.get_high()));
        WriteBuf wb(0, 0);
        send_user(&wb, DROP, send_window, range.get_high());
    } else if ((output.empty() && 
                (seqno_eq(input_map.get_aru_seq(), SEQNO_MAX) ||
                 !seqno_eq(input_map.get_aru_seq(), prev_aru))) &&
               !(msg.get_flags() & EVSMessage::F_RESEND)) {
        // Output queue empty and aru changed, send gap to inform others
        LOG_DEBUG("sending gap");
        send_gap(source, current_view, EVSRange(SEQNO_MAX, SEQNO_MAX));
    }
    
    deliver();
    while (get_state() == OPERATIONAL && output.empty() == false)
        if (send_user())
            break;
}

void EVSProto::handle_delegate(const EVSMessage& msg, const EVSPid& source,
                               const ReadBuf* rb, const size_t roff)
{
    EVSMessage umsg;
    umsg.read(rb->get_buf(roff), 
              rb->get_len(roff), msg.size());
    handle_user(umsg, umsg.get_source(), rb, 
                roff + msg.size() + umsg.size());
}

void EVSProto::handle_gap(const EVSMessage& msg, const EVSPid& source)
{
    LOG_DEBUG("this: " + my_addr.to_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string() + " seq: " + ::to_string(msg.get_seq()) +
              " aru_seq: " + ::to_string(msg.get_aru_seq()));
    std::map<EVSPid, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
        std::pair<std::map<const EVSPid, EVSInstance>::iterator, bool> iret;
        iret = known.insert(std::pair<const EVSPid, EVSInstance>(source,
                                                                 EVSInstance()));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) {
            shift_to(RECOVERY);
            send_join();
        }
        return;
    } else if (state == JOINING || state == CLOSED) {	
        // Silent drop
        return;
    } else if (state == RECOVERY && install_message && 
               install_message->get_source_view() == msg.get_source_view()) {
        i->second.installed = true;
        if (is_all_installed())
            shift_to(OPERATIONAL);
        return;
    } else if (!(msg.get_source_view() == current_view)) {
        if (i->second.trusted == false) {
            // Do nothing, just discard message
        } else if (i->second.operational == false) {
            // This is probably partition merge, see if it works out
            i->second.operational = true;
            shift_to(RECOVERY);
            send_join();
        } else if (i->second.installed == false) {
            // Probably caused by network partitioning during recovery
            // state, this will most probably lead to view 
            // partition/remerge. In order to do it in organized fashion,
            // don't trust the source instance during recovery phase.
            // Note: setting other instance to non-trust here is too harsh
            // LOG_WARN("Setting source status to no-trust");
            // i->second.trusted = false;
            // shift_to(RECOVERY);
            // send_join();
        } else {
            i->second.trusted = false;
            shift_to(RECOVERY);
            send_join();
        }
        return;
    }
    
    assert(i->second.trusted == true && 
           (i->second.operational == true || i->second.leaving == true) &&
           (i->second.installed == true || get_state() == RECOVERY) &&
           msg.get_source_view() == current_view);
    
    uint32_t prev_safe = input_map.get_safe_seq();
    // Update safe seq for source
    if (!seqno_eq(msg.get_aru_seq(), SEQNO_MAX))
        input_map.set_safe(source, msg.get_aru_seq());
    
    if (get_state() == RECOVERY && 
        !seqno_eq(prev_safe, input_map.get_safe_seq()))
        send_join();
    
    // Scan through gap list and resend or recover messages if appropriate.
    EVSGap gap = msg.get_gap();
    LOG_DEBUG("gap source " + gap.get_source().to_string());
    if (gap.get_source() == my_addr)
        resend(i->first, gap);
    else if (get_state() == RECOVERY && !seqno_eq(gap.get_high(), SEQNO_MAX))
        recover(gap);
    
    
    // If it seems that some messages from source instance are missing,
    // send gap message
    EVSRange source_gap(input_map.get_sa_gap(source));
    if (!seqno_eq(msg.get_seq(), SEQNO_MAX) && 
        (seqno_eq(source_gap.get_low(), SEQNO_MAX) ||
         !seqno_gt(source_gap.get_low(), msg.get_seq()))) {
        // TODO: Sending gaps here causes excessive flooding, need to 
        // investigate if gaps can be sent only in send_user
        // LOG_INFO("requesting at " + my_addr.to_string() + " from " + source.to_string() + " " + EVSRange(source_gap.get_low(), msg.get_seq()).to_string() + " due to gap message");
        // send_gap(source, current_view, EVSRange(source_gap.get_low(),
        //                                      msg.get_seq()));
    }
    
    // Deliver messages 
    deliver();
    while (get_state() == OPERATIONAL && output.empty() == false)
        if (send_user())
            break;
}


bool EVSProto::states_compare(const EVSMessage& msg) 
{

    const std::map<EVSPid, EVSMessage::Instance>* instances = msg.get_instances();
    bool send_join_p = false;
    uint32_t high_seq = SEQNO_MAX;

    for (std::map<EVSPid, EVSMessage::Instance>::const_iterator 
             ii = instances->begin(); ii != instances->end(); ++ii) {
        
        std::map<EVSPid, EVSInstance>::iterator local_ii =
            known.find(ii->second.get_pid());
        
        if (local_ii->second.operational != ii->second.get_operational()) {
            if (local_ii->second.operational == true) {
                if (local_ii->second.tstamp + inactive_timeout <
                    Time::now()) {
                    LOG_DEBUG("setting " + local_ii->first.to_string() 
                              + " as unoperational at " + my_addr.to_string());
                    local_ii->second.operational = false;
                    send_join_p = true;
                }
            } else {
                send_join_p = true;
            }
        }
        
        if (local_ii->second.trusted != ii->second.get_trusted()) {
            if (local_ii->second.trusted == true) {
                LOG_DEBUG("setting " + local_ii->first.to_string() 
                          + " as utrusted at " + my_addr.to_string());
                local_ii->second.trusted = false;
            }
            send_join_p = true;
        }
        
        // Coming from the same view
        if (ii->second.get_view_id() == current_view) {
            EVSRange range(input_map.get_sa_gap(ii->second.get_pid()));
            if (!seqno_eq(range.get_low(),
                          ii->second.get_range().get_low()) ||
                !seqno_eq(range.get_high(),
                          ii->second.get_range().get_high())) {
                assert(seqno_eq(range.get_low(), SEQNO_MAX) || 
                       !seqno_eq(range.get_high(), SEQNO_MAX));
                
                if (!seqno_eq(range.get_low(), SEQNO_MAX) &&
                    seqno_gt(range.get_high(), 
                             ii->second.get_range().get_high())) {
                    // This instance knows more
                    // TODO: Optimize to recover only required messages
                    
                    recover(EVSGap(ii->second.get_pid(), range));
                } else {
                    // The other instance knows more, sending join message
                    // should generate message recovery on more knowledgeble
                    // instance
                    send_join_p = true;
                }
            }

            // Update highest known seqno
            if (!seqno_eq(ii->second.get_range().get_high(), SEQNO_MAX) &&
                (seqno_eq(high_seq, SEQNO_MAX) || 
                 seqno_gt(ii->second.get_range().get_high(), high_seq))) {
                high_seq = ii->second.get_range().get_high();
            }
        }
    }
    
    // last locally generated seqno is not equal to high seqno, generate
    // completing dummy message
    if (!seqno_eq(high_seq, SEQNO_MAX) &&
        (seqno_eq(last_sent, SEQNO_MAX) || 
         seqno_lt(last_sent, high_seq))) {
        LOG_DEBUG("completing seqno to " + ::to_string(high_seq));
        WriteBuf wb(0, 0);
        send_user(&wb, DROP, send_window, high_seq);
    }
    
    
    return send_join_p;
}

void EVSProto::handle_join(const EVSMessage& msg, const EVSPid& source)
{
    LOG_DEBUG("this: " + my_addr.to_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string());
    std::map<const EVSPid, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
        assert(!(source == my_addr));
        LOG_DEBUG("EVSProto::handle_join(): new instance");
        std::pair<std::map<const EVSPid, EVSInstance>::iterator, bool> iret;
        iret = known.insert(std::pair<const EVSPid, EVSInstance>(source,
                                                                 EVSInstance()));
        assert(iret.second == true);
        i = iret.first;
        i->second.operational = true;
        if (state == JOINING || state == RECOVERY || state == OPERATIONAL) {
            shift_to(RECOVERY);
            send_join();
        }
        return;
    } else if (i->second.trusted == false) {
        LOG_DEBUG("untrusted");
        // Silently drop
        return;
    }
    
    if (state == RECOVERY && install_message && is_consistent(msg)) {
        LOG_DEBUG("redundant join message");
        return;
    }
    
    bool send_join_p = false;
    if (state == JOINING || state == OPERATIONAL || install_message) {
        send_join_p = true;
        shift_to(RECOVERY);
    }
    
    assert(i->second.trusted == true && i->second.installed == false);
    

    // Instance previously declared unoperational seems to be operational now
    if (i->second.operational == false) {
        i->second.operational = true;
        LOG_DEBUG("unop -> op");
        send_join_p = true;
    } 
    
    if (msg.get_source_view() == current_view) {
        uint32_t prev_safe = input_map.get_safe_seq();
        if (!seqno_eq(msg.get_aru_seq(), SEQNO_MAX))
            input_map.set_safe(source, msg.get_aru_seq());
        if (!seqno_eq(prev_safe, input_map.get_safe_seq())) {
            LOG_DEBUG("safe seq changed");
            send_join_p = true;
        }
        // Aru seqs are not the same
        if (!seqno_eq(msg.get_aru_seq(), input_map.get_aru_seq())) {
            LOG_DEBUG("noneq aru");
        }
        
        // Safe seqs are not the same
        if (!seqno_eq(msg.get_seq(), input_map.get_safe_seq())) {
            LOG_DEBUG("noneq seq, local " + ::to_string(input_map.get_safe_seq()) + " msg " + ::to_string(msg.get_seq()));
        }
    }
    
    // Store join message
    if (i->second.join_message) {
        delete i->second.join_message;
    }
    i->second.join_message = new EVSMessage(msg);
    
    
    // Converge towards consensus
    const std::map<EVSPid, EVSMessage::Instance>* instances = 
        msg.get_instances();
    std::map<EVSPid, EVSMessage::Instance>::const_iterator selfi = 
        instances->find(my_addr);
    if (selfi == instances->end()) {
        // Source instance does not know about this instance, so there 
        // is no sense to compare states yet
        LOG_DEBUG("this instance not known by source instance");
        send_join_p = true;
    } else if (selfi->second.get_trusted() == false) {
        // Source instance does not trust this instance, this feeling
        // must be mutual
        LOG_DEBUG("mutual untrust");
        i->second.trusted = false;
        send_join_p = true;
    } else if (!(current_view == msg.get_source_view())) {
        // Not coming from same views, there's no point to compare 
        // states further
        LOG_DEBUG(std::string("different view ") +
                  msg.get_source_view().to_string());
    } else {
        LOG_DEBUG("states compare");
        if (states_compare(msg))
            send_join_p = true;
    }
    
    
    if (send_join_p || (source == my_addr && is_consistent(msg) == false)) {
        LOG_DEBUG("send join");
        send_join();
    } else if (is_consensus() && is_representative(my_addr)) {
        LOG_DEBUG("is consensus and representative at " + my_addr.to_string());
        send_install();
    }
}

void EVSProto::handle_leave(const EVSMessage& msg, const EVSPid& source)
{
    std::map<EVSPid, EVSInstance>::iterator i = known.find(source);

    if (i == known.end()) {
        LOG_DEBUG("leave message from unknown source");
        return;
    }
    
    if (source == my_addr) {
        assert(state == LEAVING);
        deliver_empty_view();
        shift_to(CLOSED);
        return;
    }

    if (i->second.trusted == false) {
        return;
    } else if (!(msg.get_source_view() == current_view)) {
        return;
    }
    i->second.operational = false;
    i->second.leaving = true;
    shift_to(RECOVERY);
    send_join();
}

void EVSProto::handle_install(const EVSMessage& msg, const EVSPid& source)
{
    LOG_DEBUG("this: " + my_addr.to_string() + " source: " +
              msg.get_source().to_string() + " source view: " + 
              msg.get_source_view().to_string());
    std::map<EVSPid, EVSInstance>::iterator i = known.find(source);
    
    if (i == known.end()) {
        std::pair<std::map<const EVSPid, EVSInstance>::iterator, bool> iret;
        iret = known.insert(std::pair<const EVSPid, EVSInstance>(source,
                                                                 EVSInstance()));
        assert(iret.second == true);
        i = iret.first;
        if (state == RECOVERY || state == OPERATIONAL) {
            shift_to(RECOVERY);
            send_join();
        }
        return;	
    } else if (state == JOINING || state == CLOSED) {
        LOG_DEBUG("dropping install message from " + source.to_string());
        return;
    } else if (i->second.trusted == false) {
        LOG_DEBUG("dropping install message from " + source.to_string());
        return;
    } else if (i->second.operational == false) {
        LOG_DEBUG("setting other as operational");
        i->second.operational = true;
        shift_to(RECOVERY);
        send_join();
        return;
    } else if (install_message || i->second.installed == true) {
        LOG_DEBUG("woot?");
        shift_to(RECOVERY);
        send_join();
        return;
    } else if (is_representative(source) == false) {
        LOG_DEBUG("source is not supposed to be representative");
        shift_to(RECOVERY);
        send_join();
        return;
    } else if (is_consensus() == false) {
        LOG_DEBUG("is not consensus");
        shift_to(RECOVERY);
        send_join();
        return;
    }
    
    assert(install_message == 0);
    
    install_message = new EVSMessage(msg);
    send_gap(my_addr, install_message->get_source_view(), 
             EVSRange(SEQNO_MAX, SEQNO_MAX));
}

/////////////////////////////////////////////////////////////////////////////
// EVS interface
/////////////////////////////////////////////////////////////////////////////

void EVS::handle_up(const int cid, const ReadBuf* rb, const size_t roff, 
                    const ProtoUpMeta* um)
{
    EVSMessage msg;
    
    if (rb == 0 && um == 0)
        throw FatalException("EVS::handle_up(): Invalid input rb == 0 && um == 0");
    const TransportNotification* tn = 
        static_cast<const TransportNotification*>(um);
    
    if (rb == 0 && tn) {
    // if (proto->my_addr == ADDRESS_INVALID)
    //   proto->my_addr = tp->get_sockaddr();
        proto->handle_notification(tn);
        return;
    }
    
    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
        LOG_WARN("EVS::handle_up(): Invalid message");
        return;
    }
    
    EVSPid source(msg.get_source());
    
    switch (msg.get_type()) {
    case EVSMessage::USER:
        proto->handle_user(msg, source, rb, roff);
        break;
    case EVSMessage::DELEGATE:
        proto->handle_delegate(msg, source, rb, roff);
        break;
    case EVSMessage::GAP:
        proto->handle_gap(msg, source);
        break;
    case EVSMessage::JOIN:
        proto->handle_join(msg, source);
        break;
    case EVSMessage::LEAVE:
        proto->handle_leave(msg, source);
        break;
    case EVSMessage::INSTALL:
        proto->handle_install(msg, source);
        break;
    default:
        LOG_WARN(std::string("EVS::handle_up(): Invalid message type: ") 
                 + to_string(msg.get_type()));
    }    
}

int EVS::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    return proto->handle_down(wb, dm);
}

void EVS::join(const ServiceId sid, Protolay *up)
{
    proto->set_up_context(up);
    proto->shift_to(EVSProto::JOINING);
}

void EVS::leave(const ServiceId sid)
{
    proto->shift_to(EVSProto::LEAVING);
}

void EVS::connect(const char* addr)
{
    tp->connect(addr);
    proto = new EVSProto(tp, ADDRESS_INVALID);
}

void EVS::close()
{
    tp->close();
}


EVS* EVS::create(const char *addr, Poll *p)
{
    if (strncmp(addr, "tipc:", strlen("tipc:")) != 0)
        throw FatalException("EVS: Only TIPC transport is currently supported");
    
    EVS *evs = new EVS();
    evs->tp = Transport::create(addr, p, evs);
    evs->set_down_context(evs->tp);
    return evs;
}
