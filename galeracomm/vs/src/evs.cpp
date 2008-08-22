
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



struct EVSGap {
    Sockaddr source;
    EVSRange range;
};



static struct EVSMessageLstr {
    bool operator()(const EVSMessage& a, const EVSMessage& b) const {
	assert(a.get_type() == EVSMessage::USER && 
	       b.get_type() == EVSMessage::USER);
	if (a.get_seq() < b.get_seq())
	    return true;
	if (a.get_seq() > b.get_seq())
	    return false;
	return a.get_source() < b.get_source();
    }
} EVSMessageLstr;


struct EVSInstance {
    // True if instance is considered to be operational (has produced messages)
    bool operational;
    // True if instance can be trusted (is reasonably well behaved)
    bool trusted;
    // Known aru map of the instance
    // Commented out, this is needed only on recovery time and 
    // should be found from join message std::map<const Sockaddr, uint32_t> aru;
    // Next expected seq from the instance
    // Commented out, this should be found from input map uint32_t expected;
    // True if it is known that the instance has installed current view
    bool installed;
    // Last received JOIN message
    EVSMessage* join_message;
    // CTOR
    EVSInstance() : 
	operational(false), 
	trusted(true), 
	installed(false), join_message(0) {}
};




class EVSProto : public Protolay {
public:
    Transport* tp;
    EVSProto(Transport* t) : tp(t),
			     my_addr(Sockaddr::ADDR_INVALID), 
			     current_view(0),
			     install_message(0),
			     last_sent(SEQNO_MAX),
			     send_window(16),
			     safe_aru(SEQNO_MAX) {}
    Sockaddr my_addr;
    // 
    // Known instances 
    std::map<const Sockaddr, EVSInstance*> known;
    // Current view id
    EVSViewId* current_view;

    // Map containing received messages and aru/safe seqnos
    EVSInputMap input_map;
    
    // Last received install message
    EVSMessage* install_message;
    // 
    uint32_t last_delivered_safe;
    
    // Last sent seq
    uint32_t last_sent;
    // Send window size
    uint32_t send_window;
    // Output message queue
    std::deque<WriteBuf*> output;
    
    enum State {
	CLOSED,
	JOINING,
	LEAVING,
	RECOVERY, 
	OPERATIONAL,
	STATE_MAX
    };
    State state = CLOSED;

    static std::string to_string(const State s) const {
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

    
    int send_user(WriteBuf* wb, const uint32_t range = SEQNO_MAX);
    int send_user();

    void send_gap(const Sockaddr&, const uint32_t, const uint32_t);
    void send_join();
    void send_leave();
    void send_install();

    void resend(const EVSGap&);
    void recover();
    void deliver();
    
    // Update aru map, return true if map was changed, otherwise false
    bool update_aru(const Sockaddr&, std::map<Sockaddr, uint32_t>);
    
    void shift_to(const State);


    // Message handlers
    void handle_notification(const TransportNotification*);
    void handle_user(const EVSMessage&, const Sockaddr&, 
		     const ReadBuf*, const size_t);
    void handle_delegate(const EVSMEssage&, const Sockaddr&, 
			 const ReadBuf*, const size_t);
    void handle_gap(const EVSMessage&, const Sockaddr&);
    void handle_join(const EVSMessage&, const Sockaddr&);
    void handle_leave(const EVSMessage&, const Sockaddr&);
    void handle_install(const EVSMessage&, const Sockaddr&);
};

/////////////////////////////////////////////////////////////////////////////
// Message sending
/////////////////////////////////////////////////////////////////////////////

int EVSProto::send_user(WriteBuf* wb, const uint32_t up_to_seqno)
{
    int ret;
    uint32_t seq = seqno_next(last_sent);
    
    // Flow control
    if (seqno_lt(seqno_add(input_map.get_aru_seq(), send_window), seq))
	return EAGAIN;
    
    uint32_t seq_range = up_to_seqno == SEQNO_MAX ? 0 : seqno_dec(up_to_seqno, seq);
    assert(seq_range < 0x100U);
    EVSMessage msg(EVSMessage::USER, seq, seq_range & 0xffU, 
		   input_map.get_aru_seq(), *current_view, 0);
    
    wb->prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
    if ((ret = pass_down(wb, 0)) == 0) {
	last_sent = seq;
	ReadBuf* rb = wb->to_readbuf();
	EVSRange range = input_map.insert(EVSInputMapItem(my_addr, msg, rb));
	assert(seqno_eq(range.high, last_sent));
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
    if ((ret = send_user(wb)) == 0) {
	output.pop_front();
	delete wb;
    }
    return ret;
}

int EVSProto::send_delegate(const ReadBuf* rb)
{
    // TODO:
    return ENOPROTOSUPP;
}

void EVSProto::send_gap(const Sockaddr& target, const uint32_t expected, 
			const uint32_t current)
{
    // TODO: Investigate if gap sending can be somehow limited, 
    // message loss happen most probably during congestion and 
    // flooding network with gap messages won't probably make 
    // conditions better
    
    std::map<const Sockaddr, EVSInstance*>::iterator i = known.find(my_addr);
    assert(i != known.end());
    EVSMessage gm(EVSMessage::GAP, i->second.last_sent, target, expected, current);
    
    size_t bufsize = gm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (gm.write(buf, bufsize, 0) == 0)
	throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
	LOG_WARN(std::string("EVSProto::send_leave(): Send failed ") 
		 + strerror(err));
    }
    delete[] buf;
}


void EVSProto::send_join()
{
    EVSMessage jm(EVSMessage::JOIN, 
		  current_view ? *current_view : EVSViewId(my_addr, 0), 
		  input_map.get_aru_seq(), input_map.get_safe_seq());
    for (std::map<const Sockaddr, EVSInstance*>::iterator i = known.begin();
	 i != known.end(); ++i) {
	if (i->second->trusted && i->second->operational) {
	    jm.add_operational_instance(i->first, input_map.get_sa_gap());
	} else if (i->second->trusted == false) {
	    jm.add_untrusted_instance(i->first);
	} else if (i->second->operational == false) {
	    jm.add_unoperational_instance(i->first);
	}
    }
    size_t bufsize = jm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (jm.write(buf, bufsize, 0) == 0)
	throw FatalException("");
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
	LOG_WARN(std::string("EVSProto::send_join(): Send failed ") 
		 + strerror(err));
    }
    delete[] buf;
}

void EVSProto::send_leave()
{
    EVSMessage lm(EVSMessage::LEAVE, *current_view);
    size_t bufsize = lm.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (lm.write(buf, bufsize, 0) == 0)
	throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
	LOG_WARN(std::string("EVSProto::send_leave(): Send failed ") 
		 + strerror(err));
    }
    delete[] buf;
}

void EVSProto::send_install()
{
    std::map<const Sockaddr, EVSInstance*>::iterator self = known.find(my_addr);
    EVSMessage im(EVSMessage::INSTALL, 
		  EVSViewId(my_addr, current_view ? 
			    current_view->seq + 1 : 1),
		  input_map.get_aru_seq(), input_map.get_safe_seq());
    for (std::map<const Sockaddr, EVSInstance*>::iterator i = known.begin();
	 i != known.end(); ++i) {
	if (i->second->trusted && i->second->operational)
	    im.add_operational_instance(i->first);
	else if (i->second->trusted == false)
	    im.add_untrusted_instance(i->first);
	else if (i->second->operational == false)
	    im.add_unoperational_instance(i->first);
    }
    size_t bufsize = im.size();
    unsigned char* buf = new unsigned char[bufsize];
    if (im.write(buf, bufsize, 0) == 0)
	throw FatalException("");
    
    WriteBuf wb(buf, bufsize);
    int err;
    if ((err = pass_down(&wb, 0))) {
	LOG_WARN(std::string("EVSProto::send_leave(): Send failed ") 
		 + strerror(err));
    }
    delete[] buf;
}


void EVSProto::resend(const Sockaddr& gap_source, const EVSGap& gap)
{

    assert(gap.source == my_addr);
    if (gap.lwm == gap.hwm) {
	LOG_DEBUG(std::string("EVSProto::resend(): Empty gap") 
		  + to_string(gap.lwm) + " -> " 
		  + to_string(gap.hwm));
	return;
    }
    
    for (uint32_t seq = gap.low; !seqno_gt(seq, gap.high); 
	 seq = seqno_next(seq)) {
	EVSInputMap::iterator i = input_map.find(my_addr, seq);
	if (i == input_map.end()) {
	    IMap::iterator ii = known.find();
	    assert(ii != known.end());
	    ii->second.trusted = false;
	    shift_to(RECOVERY);
	}
    }
}

void EVSProto::recover(const EVSGap& gap)
{

    if (gap.lwm == gap.hwm) {
	LOG_WARN(std::string("EVSProto::recover(): Empty gap: ") 
		 + to_string(gap.lwm) + " -> " 
		 + to_string(gap.hwm));
	return;
    }

    std::map<const Sockaddr, EVSInstance*>::iterator inst = 
	get_most_updated_for(gap.source);
    
    if (inst->first == my_addr) {
	InputMap::iterator i = inst->second->input.find(
	    gap.lwm == SEQNO_MAX ? 0 : gap.lwm);
	if (i == inst->second->input.end()) {
	    // Even the most updated does not have all messages from
	    // gap source, untrust the source.
	    LOG_WARN(std::string("EVSProto::resend(): Can't resend the gap: ")
		     + to_string(gap.lwm) + " -> " 
		     + to_string(gap.hwm));
	    std::map<const Sockaddr, EVSInstance*>::iterator gsi = 
		known.find(gap.source);
	    gsi->second->trusted = false;
	    shift_to(RECOVERY);
	} else {
	    for (; i != self->second->input.end(); ++i) {
		WriteBuf wb(i->second->second->get_buf(),
			    i->second->second->get_len());
		if (pass_down(&wb, 0))
		    break;
		if (seqno_next(i->first) == gap.hwm)
		    break;
	    }
	}
    }
}

////////////////////////////////////////////////////////////////////////
// Protolay interface
////////////////////////////////////////////////////////////////////////

int EVSProto::handle_down(WriteBuf* wb, const ProtoDownMeta* dm)
{
    int ret = 0;
    if (output.empty()) {
	int err = send_user(wb);
	switch (err) {
	case EAGAIN:
	    WriteBuf* priv_wb = wb->copy();
	    output.push_back(priv_wb);
	    // Fall through
	case 0:
	    break;
	default:
	    LOG_ERR(std::string("Send error: ") + to_string(err));
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

void EVSProto::shift_to(const State& s)
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
	LOG_FATAL(std::string("EVSProto::shift_to(): "
			      "Invalid state transition: ") 
		  + to_string(state) + " -> " + to_string(s));
	throw FatalException("Invalid state transition");
    }

    LOG_INFO(std::string("EVS::shift_to(): State change: ") + 
	     to_string(state) + " -> " + to_string(s));

    switch (s) {
    case CLOSED:
	cleanup();
	state = CLOSED;
	break;
    case JOINING:
	// Haven't got address from transport yet, this usually means
	// asynchronous transport connect operation. Join is sent once
	// connect notification is passed up
	if (my_addr != Sockaddr::ADDR_INVALID)
	    send_join();
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
	send_join();
	state = RECOVERY;
	break;
    case OPERATIONAL:
	assert(is_consensus() == true);
	assert(all_installed() == true);
	deliver();
	deliver_trans_view();
	deliver_trans();
	deliver_reg_view();
	// Reset input map
	input_map.clear();
	for (IMap::iterator i = known.begin(); i != known.end(); ++i)
	    if (i->second.installed)
		input_map.insert_sa(i->first);
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
    if (state != OPERATIONAL || state != RECOVERY)
	throw FatalException("Invalid state");
    assert(current_view);
    EVSInputMap::iterator i, i_next;
    // First deliver all messages that qualify at least as safe
    for (i = input_map.begin();
	 i != input_map.end() && input_map.is_safe(i); i = i_next) {
	i_next = i;
	++i_next;
	assert(current_view->find(i->get_sockaddr()));
	EVSProtoUpMeta um(i->get_sockaddr());
	pass_up(i->get_readbuf(), i->get_readbuf_offset(), &um);
	input_map.erase(i);
    }
    // Deliver all messages that qualify as agreed
    for (; i != input_map.end() && 
	     i->get_evs_message().get_safety_prefix() == EVSMessage::AGREED &&
	     input_map.is_agreed(i); i = i_next) {
	i_next = i;
	++i_next;
	assert(current_view->find(i->get_sockaddr())); 
	EVSProtoUpMeta um(i->get_sockaddr());
	pass_up(i->get_readbuf(), i->get_readbuf_offset(), &um);
	input_map.erase(i);
    }
    // And finally FIFO or less 
    for (; i != input_map.end() &&
	     i->get_evs_message().get_safety_prefix() == EVSMessage::FIFO &&
	     input_map.is_fifo(i); i = i_next) {
	i_next = i;
	++i_next;
	assert(current_view->find(i->get_sockaddr())); 
	EVSProtoUpMeta um(i->get_sockaddr());
	pass_up(i->get_readbuf(), i->get_readbuf_offset(), &um);
	input_map.erase();
    }
}

void EVSProto::deliver_trans()
{
    if (state != RECOVERY)
	throw FatalException("Invalid state");
    assert(current_view);
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
	     i->get_evs_message().get_safety_prefix() == EVSMessage::AGREED &&
	     input_map.is_agreed(i); i = i_next) {
	i_next = i;
	++i_next;
	assert(current_view->find(i->get_sockaddr())); 
	EVSProtoUpMeta um(i->get_sockaddr());
	pass_up(i->get_readbuf(), i->get_readbuf_offset(), &um);
	input_map.erase(i);
    }
    
    for (; i != input_map.end() &&
	     i->get_evs_message().get_safety_prefix() == EVSMessage::FIFO &&
	     input_map.is_fifo(i); i = i_next) {
	i_next = i;
	++i_next;
	assert(known.find(i->get_sockaddr()) && 
	       known.find(i->get_sockaddr())->second.installed); 
	EVSProtoUpMeta um(i->get_sockaddr());
	pass_up(i->get_readbuf(), i->get_readbuf_offset(), &um);
	input_map.erase();
    }
    
    // Sanity check:
    // There must not be any messages left that 
    // - Are originated from outside of trans conf and are FIFO
    // - Are originated from trans conf
    for (i = input_map.begin(); i != input_map.end(); i = i_next) {    
	i_next = i;
	++i_next;
	IMap::iterator ii = known.find();
	if (ii->second.installed)
	    throw FatalException("Protocol error in transitional delivery (self delivery constraint)");
	else if (input_map.is_fifo(i))
	    throw FatalException("Protocol error in transitional delivery (fifo  from partitioned component)");
	input_map.erase(i);
    }
}


/////////////////////////////////////////////////////////////////////////////
// Message handlers
/////////////////////////////////////////////////////////////////////////////

void EVSProto::handle_notification(const TransportNotification *tn)
{
    std::map<Sockaddr, EVSInstance>::iterator i = known.find(tn->source_sa);

    if (i == known.end() && tn->ntype == TRANSPORT_N_SUBSCRIBED) {
	known.insert(std::pair<Sockaddr, EVSInstance >(source, EVSInstance()));
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
    if (my_addr != Sockaddr::ADDR_INVALID || state == JOINING)
	send_join();
}

void EVSProto::handle_user(const EVSMessage& msg, const Sockaddr& source, 
			   const ReadBuf* rb, const size_t roff)
{

    std::map<Sockaddr, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
	// Previously unknown instance has appeared and it seems to
	// be operational, assume that it can be trusted and start
	// merge/recovery
	i = known.insert(std::pair<Sockaddr, EVSInstance());
	i->second->operational = true;
	if (state == RECOVERY || state == OPERATIONAL)
	    shift_to(RECOVERY);
	return;
    } else if (state == JOINING || state == CLOSED) {
	// Drop message
	return;
    } else if (current_view == 0) {
	if (state != RECOVERY)
	    throw FatalException("No view in OPERATIONAL state");
	// Drop message
	return;
    } else if (msg.get_source_view() != *current_view) {
	if (i->second->trusted == false) {
	    // Do nothing, just discard message
	    return;
	} else if (i->second->operational == false) {
	    // This is probably partition merge, see if it works out
	    i->second->operational = true;
	    shift_to(RECOVERY);
	    return;
	} else if (i->second->installed == false) {
	    if (install_message && 
		msg.get_source_view() == install_message->get_view()) {
		assert(state == RECOVERY);
		// Other instances installed view before this one, so it is 
		// safe to shift to OPERATIONAL if consensus has been reached
		if (is_consensus()) {
		    shift_to(OPERATIONAL);
		} else {
		    shift_to(RECOVERY);
		}
	    } else {
		// Probably caused by network partitioning during recovery
		// state, this will most probably lead to view 
		// partition/remerge. In order to do it in organized fashion,
		// don't trust the source instance during recovery phase.
		LOG_WARN("Setting source status to no-trust");
		i->second->trusted = false;
		shift_to(RECOVERY);
		return;
	    }
	} else {
	    i->second->trusted = false;
	    shift_to(RECOVERY);
	    return;
	}
    }

    assert(i->second->trusted == true && 
	   i->second->operational == true &&
	   i->second->installed == true &&
	   current_view &&
	   msg.get_source_view() == *current_view);
    
    
    EVSRange gap(input_map.insert(msg, rb));

    // Some messages are missing
    if (seqno_gt(gap.high, gap.low))
	send_gap(i->first, gap);
    

    if (i->first != my_addr && (
	    (output.empty() && !(msg.get_flags() & EVSMessage::F_MSG_MORE)) ||
	    state == RECOVERY
	    ) && seqno_lt(seqno_next(last_sent), gap.low)) {
	// Message not originated from this instance, output queue is empty
	// and last_sent seqno should be advanced
	WriteBuf wb(0, 0);
	send_user(&wb, gap.low);
    } else if (output.empty() && !seqno_eq(input_map.get_aru_seq(), 
					   input_map.get_safe_seq())) {
	resend(EVSGap(my_addr, EVSRange(last_sent, last_sent)));
    }
    
    deliver();
    while (state == OPERATIONAL && output.empty() == false)
	if (send_user())
	    break;
}

void EVSProto::handle_delegate(const EVSMessage& msg, const Sockaddr& source,
			       const ReadBuf* rb, const size_t roff)
{
    EVSMessage umsg = msg.get_user_msg();
    handle_user(umsg, msg.get_user_source(), rb, msg.get_user_msg_offset());
}

void EVSProto::handle_gap(const EVSMessage& msg, const Sockaddr& source)
{
    std::map<Sockaddr, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
	i = known.insert(std::pair<Sockaddr, EVSInstance());
	i->second->operational = true;
	if (state == RECOVERY || state == OPERATIONAL)
	    shift_to(RECOVERY);
	return;
    } else if (state == JOINING || state == CLOSED) {	
	// Silent drop
	return;
    } else if (state == RECOVERY && install_message && 
	       install_message->get_view() == msg.get_source_view()) {
	i->second->installed = true;
	if (all_installed())
	    shift_to(OPERATIONAL);
    } else if (current_view == 0) {
	// This message has no use
	return;
    } else if (msg.get_source_view() != *current_view) {
	if (i->second->trusted == false) {
	    // Do nothing, just discard message
	} else if (i->second->operational == false) {
	    // This is probably partition merge, see if it works out
	    i->second->operational = true;
	    shift_to(RECOVERY);
	} else if (i->second->installed == false) {
	    // Probably caused by network partitioning during recovery
	    // state, this will most probably lead to view 
	    // partition/remerge. In order to do it in organized fashion,
	    // don't trust the source instance during recovery phase.
	    LOG_WARN("Setting source status to no-trust");
	    i->second->trusted = false;
	    shift_to(RECOVERY);
	} else {
	    i->second->trusted = false;
	    shift_to(RECOVERY);
	}
	return;
    }

    assert(i->second->trusted == true && 
	   i->second->operational == true &&
	   i->second->installed == true &&
	   current_view &&
	   msg.get_source_view() == *current_view);

    // Scan through gap list and resend or recover messages if appropriate.
    std::list<EVSGap> gap = msg.get_gap();
    for (std::list<EVSGap>::iterator g = gap.begin(); g != gap.end(); ++g) {
	if (g->source == my_addr)
	    resend(*g);
	else if (state == RECOVERY)
	    recover(*g);
    }

    // If it seems that some messages from source instance are missing,
    // send gap message
    EVSRange source_gap(input_map.get_sa_gap());
    if (seqno_next(msg.get_seq()) != input_map.get_sa_gap().low)
	send_gap(source, source_gap.low, msg.get_seq());

    // Deliver messages 
    deliver();
    while (output.empty() == false)
	if (send_user())
	    break;
}

void EVSProto::handle_join(const EVSMessage& msg, const Sockaddr& source)
{
    std::map<Sockaddr, EVSInstance>::iterator i = known.find(source);
    if (i == known.end()) {
	i = known.insert(std::pair<Sockaddr, EVSInstance());
	i->second->operational = true;
	if (state == RECOVERY || state == OPERATIONAL ||
	    (state == JOINING && source == my_addr))
	    shift_to(RECOVERY);
	return;
    } else if (i->second->trusted == false) {
	// Silently drop
	return;
    }

    if (state == OPERATIONAL || install_message)
	shift_to(RECOVERY);

    assert(i->second->trusted == true && i->second->installed == false);

    bool send_join_p = false;

    // 
    if (i->second->operational == false) {
	i->second->operational = true;
	send_join_p = true;
    } 

    // If source aru map has changed, send join
    if (update_aru(i, msg.get_aru_map())) {
	send_join_p = true;
    }

    if (seqno_next(msg.get_seq()) != i->second->expected) {
	send_gap(source, i->second->expected, msg.get_seq());
	send_join_p = true;
    } 

    if (i->second->join_message) {
	delete i->second->join_message;
    }
    i->second->join_message = new EVSMessage(msg);

    if (send_join_p) {
	send_join();
    } else if (is_consensus() && is_representative(my_addr)) {
	send_install();
    }
}

void EVSProto::handle_leave(const EVSMessage& msg, const Sockaddr& source)
{
    std::map<Sockaddr, EVSInstance>::iterator i = known.find(source);

    if (source == my_addr) {
	assert(state == LEAVING);
	shift_to(CLOSED);
	return;
    }

    if (i->second->trusted == false) {
	return;
    } else if (current_view != 0 && msg.get_source_view() != *current_view) {
	return;
    }
    if (i != known.end())
	known.erase(i);
    shift_to(RECOVERY);
}

void EVSProto::handle_install(const EVSMessage& msg, const Sockaddr& source)
{
    std::map<Sockaddr, EVSInstance>::iterator i = known.find(source);

    if (i == known.end()) {
	i = known.insert(std::pair<Sockaddr, EVSInstance());
	i->second->operational = true;
	if (state == RECOVERY || state == OPERATIONAL)
	    shift_to(RECOVERY);
	return;	
    } else if (state == JOINING || state == CLOSED) {
	// Silent drop
	return;
    } else if (i->second->trusted == false) {
	// Silent drop
	return;
    } else if (i->second->operational == false) {
	i->second->operational = true;
	shift_to(RECOVERY);
	return;
    } else if (install_message || i->second->installed == true) {
	shift_to(RECOVERY);
	return;
    } else if (is_representative(source) == false) {
	shift_to(RECOVERY);
	return;
    } else if (is_consensus() == false) {
	shift_to(RECOVERY);
	return;
    }

    assert(install_message == 0);

    install_message = new EVSMessage(msg);
    send_gap(my_addr, SEQNO_MAX, SEQNO_MAX);
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
	if (proto->my_addr == Sockaddr::ADDR_INVALID)
	    proto->my_addr = tp->get_sockaddr();
	proto->handle_notification(tn);
	return;
    }

    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0) {
	LOG_WARN("EVS::handle_up(): Invalid message");
	return;
    }

    Sockaddr source(&tn.source_sa, tn.sa_size);

    switch (msg.get_type()) {
    case EVSMessage::USER:
	proto->handle_gap(msg, source, rb, roff);
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


void EVS::join(const ServiceId sid, Protolay *up)
{
    proto->set_up_context(up);
    proto->shift_to(JOINING);
}

void EVS::leave(const ServiceId sid)
{
    proto->shift_to(LEAVING);
}

void EVS::connect(const char* addr)
{
    tp->connect(addr);
    proto = new EVSProto(tp);
    Sockaddr addr = tp->get_sockaddr();
    if (addr != Sockaddr::ADDR_INVALID)
	proto->my_addr = addr;
}

void EVS::close()
{
    tp->close();
}

EVS::EVS()
{

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
