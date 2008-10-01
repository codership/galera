
#include "gcomm/logger.hpp"
#include "gcomm/vs.hpp"
#include "vs_backend.hpp"

#include <deque>
#include <limits>



//
//
// VSProto
//
//



class VSProto : public Protolay {
public:
    enum State {JOINING, JOINED, LEAVING, LEFT} state;
    Address addr;
    VSView *trans_view;
    VSView *reg_view;
    VSView *proposed_view;
    uint32_t next_seq;
    VSMembMap membs;
    std::deque<std::pair<ReadBuf *, size_t> > trans_msgs;
    std::deque<ReadBuf*> local_msgs;
    const Serializable *user_state;
    VSProto(const Address a, Protolay *up_ctx, const Serializable *us) : 
	addr(a), trans_view(0), reg_view(0), 
	proposed_view(0), next_seq(0), user_state(us) {
	set_up_context(up_ctx);
    }
    ~VSProto() {
	delete trans_view;
	delete reg_view;
	delete proposed_view;
	for (VSMembMap::iterator i = membs.begin(); i != membs.end(); ++i) {
	    delete i->second;
	}
	clear_local();
	for (std::deque<std::pair<ReadBuf*, size_t> >::iterator i = trans_msgs.begin(); i != trans_msgs.end(); ++i) {
	    i->first->release();
	}
	trans_msgs.clear();
    }
    void deliver_data(const VSMessage *, const ReadBuf *);
    void handle_conf(const VSMessage *);
    void handle_state(const VSMessage *);
    void handle_data(const VSMessage *, const ReadBuf *, const size_t, const bool);

    void handle_up(const int cid, const ReadBuf *, const size_t, const ProtoUpMeta *) {
	// Dummy
	throw DException("");
    }
    int handle_down(WriteBuf *, const ProtoDownMeta *) {
	// Also dummy
	throw DException("");
    }

    void store_local(const WriteBuf* wb);
    ReadBuf *get_local();
    void clear_local();
};


void VSProto::store_local(const WriteBuf* wb)
{
    ReadBuf* rb = wb->to_readbuf();
    local_msgs.push_back(rb);
}

ReadBuf* VSProto::get_local()
{
    ReadBuf* ret;
    if (local_msgs.empty())
	throw FatalException("Local msgs empty");
    ret = local_msgs.front();
    local_msgs.pop_front();
    return ret;
}

void VSProto::clear_local()
{
    for (std::deque<ReadBuf*>::iterator i = local_msgs.begin();
	 i != local_msgs.end(); ++i) {
	(*i)->release();
    }
    local_msgs.clear();
}

void VSProto::handle_conf(const VSMessage *cm)
{
    if (cm->get_view() == 0)
	throw DException("");	
    const VSView& view(*cm->get_view());

    if (state == LEAVING && view.is_trans() && view.get_addr().empty()) {
	VSUpMeta um(cm->get_view(), 0);
	pass_up(0, 0, &um);
	state = LEFT;
	return;
    }
	
    
    if (trans_view == 0 && reg_view == 0) {
	if (view.is_trans() == false)
	    throw DException("");
	if (proposed_view != 0)
	    throw DException("");
	trans_view = new VSView(view);
    } else if (view.is_trans()) {
	delete proposed_view;
	proposed_view = 0;
	for (VSMembMap::iterator i = membs.begin(); 
	     i != membs.end(); ++i) {
	    delete i->second->state_msg;
	    i->second->state_msg = 0;
	}
	VSView& curr_view(trans_view ? *trans_view : *reg_view);

	Aset addr_inter;
	std::set_intersection(curr_view.get_addr().begin(), 
			      curr_view.get_addr().end(), 
			      view.get_addr().begin(), view.get_addr().end(),
			      std::insert_iterator<Aset>(addr_inter, addr_inter.begin()));
	trans_view = 0;
	
	trans_view = new VSView(true, reg_view ? 
				reg_view->get_view_id() : 
				VSViewId());
	trans_view->addr_insert(addr_inter.begin(), addr_inter.end());
	if (reg_view && std::includes(reg_view->get_addr().begin(),
				      reg_view->get_addr().end(),
				      trans_view->get_addr().begin(),
				      trans_view->get_addr().end()) == false ) {
	    throw DException("");
	} 
    } else {
	if (trans_view == 0 || proposed_view != 0)
	    throw DException("");
	proposed_view = new VSView(false, view.get_view_id());
	proposed_view->addr_insert(view.get_addr().begin(), view.get_addr().end());
	VSMessage state_msg(addr,
			    proposed_view->get_view_id(), proposed_view, 
			    next_seq, user_state);
	unsigned char *buf = new unsigned char[state_msg.size()];
	state_msg.write(buf, state_msg.size(), 0);
	WriteBuf wb(buf, state_msg.size());
	VSBackendDownMeta dm(true);
	int err = pass_down(&wb, &dm);
	if (err == EAGAIN) { 
	    // Fixme: poll until there's space
	    throw DException("");
	} else if (err != 0) {
	    throw DException(strerror(errno));
	} 
	delete[] buf;
    }
}

void VSProto::deliver_data(const VSMessage *dm, 
			   const ReadBuf *rb)
{
    VSMembMap::iterator membs_p = membs.find(dm->get_source());
    // std::cerr << "Message: Source(" << dm->get_source() << ")\n";
    if (membs_p == membs.end())
	throw FatalException("Message source not in memb"); 
    if (membs_p->second->expected_seq != dm->get_seq())
	throw FatalException("Gap in message sequence");
    membs_p->second->expected_seq = dm->get_seq() + 1;
    VSUpMeta um(dm);
    LOG_TRACE(std::string("Delivering message " + to_string(dm->get_seq())));
    pass_up(rb, dm->get_data_offset(), &um);
}


void VSProto::handle_state(const VSMessage *sm)
{
    if (proposed_view == 0 || 
	proposed_view->get_view_id() != sm->get_source_view()) {
	if (trans_view) {
	    std::cerr << "Cascading view changes, dropping state message\n";
	    return;
	} else {
	    // This should be impossible:
	    // - If proposed view exists, there must exist transitional 
	    //   configuration we are part of
	    // - If state message is originated from different backend 
	    //   reg_view, there must have been at least one intermediate
	    //   backend transitional view and thus there must exist 
	    //   VS::trans_view (VS::trans_view is dropped only after 
	    //   VS::reg_view is installed after receiving all state messages)
	    throw DException("");
	}
    }

    VSMembMap::iterator i;
    VSMemb *memb = 0;
    if ((i = membs.find(sm->get_source())) == membs.end()) {
	if (membs.insert(std::pair<const Address, VSMemb *>(
			     sm->get_source(),
			     memb = new VSMemb(sm->get_source()))).second == false) {
	    delete memb;
	    throw DException("");
	}
    } else {
	memb = i->second;
    }
    
    if (memb->state_msg) {
	throw DException("");
    }
    
    memb->state_msg = new VSMessage(*sm);
    size_t n_state_msgs = 0;
    for (i = membs.begin(); i != membs.end(); ++i) {
	if (i->second->state_msg)
	    n_state_msgs++;
    }
    if (n_state_msgs == proposed_view->get_addr().size()) {
	if (trans_msgs.size() && reg_view == 0)
	    throw DException("Trans msgs without preceding reg view");
	state = JOINED;
	VSUserStateMap smap;
	for (i = membs.begin(); i != membs.end(); ++i) {
	    if (trans_view->get_addr().find(i->first) != trans_view->get_addr().end())
		if (smap.insert(std::pair<const Address, const ReadBuf *>(
				    i->first, 
				    i->second->state_msg->get_user_state_buf())).second == false)
		    throw DException("");
	}
	VSUpMeta um(trans_view, &smap);
	pass_up(0, 0, &um);
	for (std::deque<std::pair<ReadBuf *, size_t> >::iterator tm = 
		 trans_msgs.begin();
	     trans_msgs.empty() == false; tm = trans_msgs.begin()) {
	    ReadBuf *rb = tm->first;
	    size_t roff = tm->second;
	    VSMessage dm;
	    if (dm.read(rb->get_buf(), rb->get_len(), roff) == 0) {
		throw DException("");
	    }
	    
	    deliver_data(&dm, rb);
	    trans_msgs.pop_front();
	    rb->release();
	}
	
	// Erase members that have left
	VSMembMap::iterator i_next;
	for (i = membs.begin(); i != membs.end(); i = i_next) {
	    i_next = i, ++i_next;
	    if (proposed_view->get_addr().find(i->first) == 
		proposed_view->get_addr().end()) {
		delete i->second;
		membs.erase(i);
	    }
	}
	
	// This must be done in the case of network partition/merge
	for (i = membs.begin(); i != membs.end(); ++i) {
	    if (i->second->expected_seq != i->second->state_msg->get_seq() &&
		trans_view->get_addr().find(i->first) != trans_view->get_addr().end()) {
		std::cerr << i->first << " expected_seq: ";
		std::cerr << i->second->expected_seq;
		std::cerr << " state_msg_seq: " << i->second->state_msg->get_seq() << "\n";
		throw DException("");
	    }
	    i->second->expected_seq = i->second->state_msg->get_seq();
	}
	
	delete trans_view;
	trans_view = 0;
	
	delete reg_view;
	reg_view = proposed_view;
	proposed_view = 0;
	
	smap.clear();
	for (i = membs.begin(); i != membs.end(); ++i) {
	    if (reg_view->get_addr().find(i->first) != reg_view->get_addr().end())
		if (smap.insert(std::pair<const Address, const ReadBuf *>(
				    i->first, 
				    i->second->state_msg->get_user_state_buf())).second == false)
		    throw DException("");
	}
	VSUpMeta umr(reg_view, &smap);
	pass_up(0, 0, &umr);
    }
}

void VSProto::handle_data(const VSMessage *dm, const ReadBuf *rb, const size_t roff, const bool be_drop_own_data)
{
    if (reg_view == 0)
	return;

    // Verify send view correctness 
    if (!(dm->get_source_view() == reg_view->get_view_id()))
	throw DException("");

    if (dm->get_source() == addr && be_drop_own_data) {
	ReadBuf* own_rb = get_local();
	VSMessage own_dm;
	if (own_dm.read(own_rb->get_buf(), own_rb->get_len(), 0) == 0) {
	    LOG_FATAL("Could not reconstruct own message");
	    throw FatalException("Corrupted message");
	}
	if (!(own_dm == *dm)) {
	    LOG_FATAL("Locally stored message does not match to received");
	    throw FatalException("Invalid or corrupted message");
	}
	if (trans_view) {
	    trans_msgs.push_back(std::pair<ReadBuf*, size_t>(own_rb, 0));
	} else {
	    deliver_data(&own_dm, own_rb);
	    own_rb->release();
	}
    } else {
	// Transitional configuration
	if (trans_view) {
	    trans_msgs.push_back(std::pair<ReadBuf*, size_t>(rb->copy(), roff));
	} else {
	    deliver_data(dm, rb);
	}
    }
}

//
//
// VS
//
//

VS::VS() : be(0), be_addr(0), mon(0)
{

}

VS::VS(Monitor *m) : be(0), be_addr(0), mon(m)
{

}

static void release_proto(std::pair<const ServiceId, VSProto *> p)
{
    delete p.second;
}

VS::~VS()
{
    free(be_addr);
    delete be;
    for_each(proto.begin(), proto.end(), release_proto);
}

void VS::connect()
{
    if (!be)
	throw DException("Not initialized");
    be->connect(be_addr);
}


void VS::close()
{
    if (!be)
	throw DException("Not connected");
    be->close();
}

void VS::join(const ServiceId sid, Protolay *up_ctx, const Serializable *user_state)
{
    if (!be)
	throw DException("Not connected");

    Critical crit(mon);
    VSProtoMap::iterator i;
    if ((i = proto.find(sid)) != proto.end())
	throw DException("Already joined");
    VSProto *p = new VSProto(
	Address(be->get_self().get_proc_id(), sid,
		be->get_self().get_segment_id()), up_ctx, user_state);


    if (proto.insert(std::pair<const ServiceId, VSProto *>(
			 sid, p)).second == false) {
	delete p;
	throw DException("");
    }
    p->set_down_context(be);
    p->state = VSProto::JOINING;
    be->join(sid);
}

void VS::leave(const ServiceId sid)
{
    if (!be)
	throw DException("Not connected");
    Critical crit(mon);
    VSProtoMap::iterator i;
    if ((i = proto.find(sid)) == proto.end())
	throw DException("Not joined");
    i->second->state = VSProto::LEAVING;
    be->leave(sid);
}

Address VS::get_self() const
{
    if (be == 0)
	throw DException("Not connected");
    return be->get_self();
}



void VS::handle_up(const int cid, const ReadBuf *rb, const size_t roff, const ProtoUpMeta *um)
{
    VSMessage msg;
    VSProto *p = 0;

    if (rb == 0) {
	throw DException("");
    }
    
    LOG_TRACE("VS:handle_up()");
    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0)
	throw FatalException("Failed to read message");
    LOG_TRACE(std::string("VS::handle_up(): Msg type = ") + to_string(msg.get_type()));

    Critical crit(mon);
    VSProtoMap::iterator pi = proto.find(msg.get_source().get_service_id());
    if (pi == proto.end()) {
	std::cerr << "Dropping message, no protoent\n";
	return;
    }
    p = pi->second;
    switch (msg.get_type()) {
    case VSMessage::CONF:
	p->handle_conf(&msg);
	break;
    case VSMessage::STATE:
	p->handle_state(&msg);
	break;
    case VSMessage::DATA:
	p->handle_data(&msg, rb, roff, be->get_flags() & VSBackend::F_DROP_OWN_DATA);
	break;
    default:
	std::cerr << "Unknown message type\n";
	throw DException("");
    }
    if (p->state == VSProto::LEFT) {
	delete p;
	proto.erase(pi);
    }
}



int VS::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    VSProto *p = 0;
    VSProtoMap::iterator pi;

    const VSDownMeta *vdm = 0;
    if (dm)
	vdm = static_cast<const VSDownMeta *>(dm);
    ServiceId sid = vdm ? vdm->sid : 0;
    uint8_t user_type = vdm ? vdm->user_type : 0;

    Critical crit(mon);
    if ((pi = proto.find(sid)) == proto.end())
	return EINVAL;

    p = pi->second;
    if (p->reg_view == 0) {
	LOG_WARN(std::string("VS::handle_down(): ") + strerror(ENOTCONN));
	return ENOTCONN;
    }

    
    // Transitional configuration
    if (p->trans_view) {
	LOG_INFO(std::string("VS::handle_down(): ") + strerror(EAGAIN));
	return EAGAIN;
    }

    VSMembMap::iterator mi = p->membs.find(p->addr);
    if (mi == p->membs.end())
	throw FatalException("VS::handle_down(): Internal error");
    if (mi->second->expected_seq + 256 == p->next_seq) {
	LOG_DEBUG(std::string("VS::handle_down(), flow control: ") + 
		    strerror(EAGAIN));
	return EAGAIN;
    }
    
    VSMessage msg(p->addr, p->reg_view->get_view_id(), p->next_seq, user_type);
    wb->prepend_hdr(msg.get_hdr(), msg.get_hdrlen());
    
    int ret = pass_down(wb, 0);
    if (ret == 0) {
	LOG_TRACE(std::string("Sent message ") + to_string(p->next_seq));
	p->next_seq++;
	if (be->get_flags() & VSBackend::F_DROP_OWN_DATA) {
	    p->store_local(wb);
	}
    }
    wb->rollback_hdr(msg.get_hdrlen());
    if (ret)
	LOG_WARN(std::string("VS::handle_down(), returning ") 
		       + strerror(ret));
    return ret;
}


VS *VS::create(const char *conf, Poll *poll, Monitor *m)
{

    VS *vs = 0;

    try {
	vs = new VS(m);
	vs->be = VSBackend::create(conf, poll, vs);
	if (::getenv("VS_RECV_ALL_DATA") == 0)
	    vs->be->set_flags(VSBackend::F_DROP_OWN_DATA);
	vs->be_addr = strdup(conf);
	vs->set_down_context(vs->be);
    } catch (Exception e) {
	delete vs;
	throw;
    }

    return vs;
}
