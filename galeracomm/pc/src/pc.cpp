


#include "gcomm/pc.hpp"


struct PCMessage {
    PCId pcid;     // Only in CREATE, JOIN, LEAVE
    PCMemb source; // Used only in CREATE, JOIN, LEAVE
    size_t raw_len;
    unsigned char raw[32]; // Remember to increase if more stuff is added

    enum Type {NONE, CREATE, JOIN, LEAVE, DATA};
    PCMessage() : raw_len(0) {
    }
    PCMessage(const PCId pi, const PCMemb s) : pcid(pi), source(s) {
    }
};


// Handlers that modify data in total order
void PC::handle_user_msg(const ReadBuf *rb, const size_t roff, 
			 const VSUpMeta *vum)
{
    PCUpMeta pum(vum->msg);
    pass_up(rb, roff, &pum);
}

void PC::handle_memb_msg(const ReadBuf *rb, const size_t roff, 
			 const VSUpMeta *vum)
{
    PCMessage pmsg;
    if (pmsg.read(rb->get_buf(roff), rb->get_len(roff), 0) == 0)
	throw DException("Invalid memb memssage");
    // If pcid does not match, drop message and throw if the message
    // was originated from this instance
    if (pmsg.pcid != pcid) {
	if (vum->msg->get_source() == vs->get_self())
	    throw DExeption("Tried to operate with invalid pcid");
	else
	    return; // Silently drop
    }
    switch (vum->msg->get_user_type()) {
    case PCMessage::CREATE: {
	if (pmsg.seq < seq) {
	    if (vum->msg->get_source() == vs->get_self())
		throw DException("Tried to create pc, but there have been incarnations in between");
	    else
		return; // Silently drop
	}
	
	if (pc.size() > 0)
	    throw DException("");
	
	std::pair<PCMembMap::iterator, bool> ret = 
	    pc.insert(std::pair<const Address, PCMemb>(
			  vum->msg->get_source(),
			  pmsg.source));
	if (ret.second == false)
	    throw DException("");
	ret.first->state = JOINED;
	if (vum->msg->get_source() == vs->get_self()) {
	    if (state != CREATING)
		throw DException("");
	    state = JOINED;
	}
	break;
    }
    case PCMessage::JOIN: {
	if (pc.size() == 0) {
	    if (vum->msg->get_source() == vs->get_self())	    
		throw DException("");
	    else 
		return;
	}
	std::pair<PCMembMap::iterator, bool> ret = 
	    pc.insert(std::pair<const Address, PCMemb>(
			  vum->msg->get_source(),
			  pmsg.source));
	if (ret.second == false)
	    throw DException("");
	ret.first->state = JOINED;
	if (vum->msg->get_source() == vs->get_self()) {
	    if (state != JOINING)
		throw DException("");
	    state = JOINED;
	}
	break;
    }
    case PCMessage::LEAVE: {
	if (pc.size() == 0) {
	    if (vum->msg->get_source() == vs->get_self())	    
		throw DException("");
	    else 
		return;
	}
	if (pc.find(vum->msg->get_source()) == pc.end())
	    throw DException("");
	pc.erase(vum->msg->get_source());
	if (vum->msg->get_source() == vs->get_self()) {
	    if (state != LEAVING)
		throw DException("");
	    state = CONNECTED;
	}
	break;
    }
    }
}
    
void PC::handle_msg(const ReadBuf *rb, const size_t roff, const VSUpMeta *vum) {
    switch (state) {
    case PCMemb::JOINED:
    case PCMemb::LEAVING:
	if (vum->msg->get_user_type() == PCMessage::DATA)
	    handle_user_msg(rb, roff, vum);
    case PCMemb::CONNECTED:
    case PCMemb::CREATING:
    case PCMemb::JOINING:
	if (vum->msg->get_user_type() != PCMessage::DATA)
	    handle_memb_msg(rb, roff, vum);
	break;
    }
}

void PC::handle_view(const VSView *view, const VSUserStateMap *smap)
{
    if (view->is_trans()) {
	assert(trans_view == 0);
	trans_view = new VSView(*view);
	
	switch (state) {
	case PCMemb::CONNECTING:
	    if (trans_view->size() > 0)
		throw DException("Non-zero view in closed state");
	    // Wait for reg conf before CONNECTED
	    break;
	case PCMemb::CONNECTED:
	case PCMemb::CREATING:
	case PCMemb::JOINING:
	case PCMemb::JOINED:
	case PCMemb::LEAVING:
	    // Reset states will determine the next state and
	    // deliver pc change notification if applicable
	    reset_states(view, smap);
	    break;
	default:
	    throw DException("Illegal state for trans view");
	}
    } else {
	delete reg_view;
	reg_view = new VSView(*view);
	
	switch (state) {
	case PCMemb::CONNECTING:
	    state = CONNECTED;
	case PCMemb::CONNECTED:
	case PCMemb::JOINED:
	    // Install states will determine the next state and deliver
	    // pc change notification if applicable
	    install_states(view, smap);
	    break;
	default:
	    // These states should not be possible... message changing the 
	    // state should have been delivered in previous transitional 
	    // configuration (guaranteed by VS)
	    throw DException("Illegal state for reg view");
	}
	
	delete trans_view;
	trans_view = 0;
    }

}

void PC::handle_up(const ReadBuf *rb, const size_t roff, 
		   const ProtoUpMeta *um)
{
    if (state == CLOSED)
	throw DException("Handle up called in closed state");
    const VSUpMeta *vum = static_cast<const VSUpMeta *>(um);
    if (vum->msg)
	handle_msg(rb, roff, vum);
    else if (vum->view)
	handle_view(vum->view, vum->state_map);
    
}

void PC::connect()
{
    vs->connect();
    vs->join(0, this, user_state);
    state = CONNECTING;
}

void PC::close()
{
    vs->leave(0);
    vs->close();
    state = CLOSED;
}

void PC::create(const char *id)
{
    if (state != CONNECTED)
	throw DException("");

    int err;
    // In this loop we either reach regular view and manage to send 
    // create message or someone manages to create pc before us.
    do {
	if (pc.size() > 0)
	    throw DException("");
	PCMessage cmsg(PCId(id), PCMemb(vs->get_self(), "", state));
	WriteBuf wb(cmsg.get_raw(), cmsg.get_raw_len());
	VSDownMeta dm(0, PCMessage::CREATE);
	err = pass_down(&wb, &dm);
	if (poll) {
	    if (poll->poll(1) < 0)
		throw DException("");
	}
    } while (poll && err == EAGAIN);
}


void PC::join(const char *id)
{
    if (state != CONNECTED || pc.size() == 0)
	throw DException("");
    
    int err;
    // In this loop we either reach regular view and manage to send 
    // create message or someone manages to create pc before us.
    do {
	if (pc.size() == 0)
	    throw DException("");
	PCMessage cmsg(PCId(id), PCMemb(vs->get_self(), "", state));
	WriteBuf wb(cmsg.get_raw(), cmsg.get_raw_len());
	VSDownMeta dm(0, PCMessage::JOIN);
	err = pass_down(&wb, &dm);
	if (poll) {
	    if (poll->poll(1) < 0)
		throw DException("");
	}
    } while (poll && err == EAGAIN);
}

void PC::leave()
{
    if (state != JOINED || pc.size() == 0)
	throw DException("");
    
    int err;
    // In this loop we either reach regular view and manage to send 
    // leave message or we reach non-primary.
    do {
	if (pc.size() == 0)
	    return;
	PCMessage cmsg(PCId(id), PCMemb(vs->get_self(), "", state));
	WriteBuf wb(cmsg.get_raw(), cmsg.get_raw_len());
	VSDownMeta dm(0, PCMessage::LEAVE);
	err = pass_down(&wb, &dm);
	if (poll) {
	    if (poll->poll(1) < 0)
		throw DException("");
	}
    } while (poll && err == EAGAIN);
}
