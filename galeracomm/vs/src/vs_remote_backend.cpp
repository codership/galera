
#include "vs_backend.hpp"
#include "vs_remote_backend.hpp"
#include "gcomm/vs.hpp"


VSRBackend::VSRBackend(Poll *p, Protolay *up_ctx) : tp(0), poll(p), state(CLOSED)
{
    set_up_context(up_ctx);
}

VSRBackend::~VSRBackend()
{
    delete tp;
}

void VSRBackend::handle_up(const int cid, const ReadBuf *rb, const size_t roff,
			   const ProtoUpMeta *um)
{
    VSRMessage msg;
    


    if (rb == 0 && tp->get_state() == TRANSPORT_S_CONNECTED) {
	return;
    } else if (rb == 0 && tp->get_state() == TRANSPORT_S_FAILED) {
	std::cerr << "Transport failed\n";
	state = FAILED;
	pass_up(0, 0, 0);
	return;
    }
    
    if (msg.read(rb->get_buf(), rb->get_len(), roff) == 0)
	throw DException("Invalid message");

    // std::cerr << "VSRBackend::handle_up(): " << rb << ":" << tp->get_state() << " message " << msg.get_type() << "\n";

    switch (msg.get_type()) {
    case VSRMessage::HANDSHAKE: {
	if (state != CONNECTING)
	    throw DException("");
	state = HANDSHAKE;
	addr = msg.get_base_address();
	if (addr == ADDRESS_INVALID)
	    throw DException("");
	VSRCommand cmd(VSRCommand::SET);
	VSRMessage rmsg(cmd);
	WriteBuf wb(0, 0);
	wb.prepend_hdr(rmsg.get_raw(), rmsg.get_raw_len());
	if (pass_down(&wb, 0)) {
	    state = FAILED;
	    pass_up(0, 0, 0);
	}
	break;
    }
	
    case VSRMessage::CONTROL: {
	const VSRCommand cmd = msg.get_command();
	if (cmd.get_result() == VSRCommand::FAIL) {
	    state = FAILED;
	    pass_up(0, 0, 0);
	} 
	else if (state == HANDSHAKE) {
	    state = CONNECTED;
	} 
	// Else we probably don't need to do anything, things just
	// happen :|
	break;
    }
    case VSRMessage::VSPROTO:
	if (state == CONNECTED)
	    pass_up(rb, roff + msg.get_raw_len(), um);
	else
	    throw DException("What's this?");
	break;
    }
}

int VSRBackend::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    if (state != CONNECTED)
	return ENOTCONN;

    VSRMessage msg;
    wb->prepend_hdr(msg.get_raw(), msg.get_raw_len());
    
    int ret = pass_down(wb, 0);
    if (ret == EAGAIN && dm) {

	const VSBackendDownMeta *bdm = static_cast<const VSBackendDownMeta *>(dm);
	if (bdm->is_sync == true) { 
	    std::cerr << "trying to clear contention\n";
	    tp->set_contention_params(10, 100);
	    ret = pass_down(wb, 0);
	    tp->set_contention_params(0, 0);
	}
    }

    wb->rollback_hdr(msg.get_raw_len());
    return ret;
}

void VSRBackend::connect(const char *addr)
{
    tp = Transport::create(addr, poll, this);
    set_down_context(tp);
    state = CONNECTING;
    
    tp->connect(addr);
    do {
	int err = poll->poll(10);
	if (err < 0)
	    throw DException("");
    } while (state == CONNECTING || state == HANDSHAKE);
}

void VSRBackend::close()
{
    if (state != CLOSED) {
	tp->close();
	delete tp;
	tp = 0;
	state = CLOSED;
    }
}

void VSRBackend::join(const ServiceId sid)
{

    Address jaddr(addr.get_proc_id(), sid, addr.get_segment_id());
    std::cerr << "JOIN " << jaddr << "\n";
    VSRCommand cmd(VSRCommand::JOIN, jaddr);
    VSRMessage msg(cmd);
    WriteBuf wb(0, 0);
    wb.prepend_hdr(msg.get_raw(), msg.get_raw_len());
    pass_down(&wb, 0);
}

void VSRBackend::leave(const ServiceId sid)
{

    Address laddr(addr.get_proc_id(), sid, addr.get_segment_id());
    std::cerr << "LEAVE " << laddr << "\n";
    VSRCommand cmd(VSRCommand::LEAVE, laddr);
    VSRMessage msg(cmd);
    WriteBuf wb(0, 0);
    wb.prepend_hdr(msg.get_raw(), msg.get_raw_len());
    pass_down(&wb, 0);
}
