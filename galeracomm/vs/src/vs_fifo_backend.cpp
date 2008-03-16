
#include "gcomm/fifo.hpp"
#include "vs_backend.hpp"
#include "vs_fifo_backend.hpp"
#include "gcomm/vs.hpp"



struct Member {
    Protolay *pl;
    enum State {JOINING, JOINED, LEAVING, CLOSED} state;
    Member(Protolay *p) : pl(p), state(JOINING) {}
};
typedef std::map<const Address, Member> MemberMap;
struct Group {
    uint32_t last_view_seq;
    std::set<MemberMap::iterator> mi;
    Group() : last_view_seq(0) {}
};
typedef std::map<const ServiceId, Group> GroupMap;

static bool operator<(const MemberMap::iterator& a,
		      const MemberMap::iterator& b) 
{
    return a->first < b->first;
}

struct ConfMessage {
    Address addr;
    enum Type {JOIN, LEAVE, PARTITION} type;
    ConfMessage(const Address a, const Type t) : addr(a), type(t) {}
    ConfMessage(const ReadBuf *rb) {
	size_t off = 0;
	uint32_t val;
	if ((off = addr.read(rb->get_buf(), rb->get_len(), off)) == 0)
	    throw DException("");
	if (read_uint32(rb->get_buf(), rb->get_len(), off, &val) == 0)
	    throw DException("");
	type = static_cast<Type>(val);
    }
    void write(WriteBuf *wb) {
	unsigned char buf[4 + 4];
	uint32_t val = type;
	size_t off = addr.write(buf, 8, 0);
	if (off == 0)
	    throw DException("");
	if (write_uint32(val, buf, 8, off) == 0)
	    throw DException("");
	wb->prepend_hdr(buf, 8);
    }
};

struct VSFifoBackendUpMeta : ProtoUpMeta {
    const VSMessage& msg;
    VSFifoBackendUpMeta(const VSMessage m) :msg(m) {}
};



class VSFifoBackendProvider : public PollContext {


    Fifo msgs;

    MemberMap members;
    GroupMap groups;

    uint16_t last_proc_id;
    SegmentId segment_id;
    Aset addresses;
    Poll *poll;

public:

    VSFifoBackendProvider(const SegmentId segid, Poll *p) : 
        last_proc_id(0), segment_id(segid), poll(p) {
	if (segid == 0)
	    throw DException("Invalid segment id");
	if (poll) {
	    poll->insert(msgs.get_read_fd(), this);
	    poll->insert(msgs.get_write_fd(), this);
	    poll->set(msgs.get_read_fd(), PollEvent::POLL_IN);
	}
    }

    ~VSFifoBackendProvider() {
	if (poll) {
	    poll->erase(msgs.get_read_fd());
	    poll->erase(msgs.get_write_fd());
	}
    }
    
    bool is_empty() const {
	return addresses.empty();
    }

    Poll *get_poll() {
	return poll;
    }

    std::pair<Address, bool> alloc_address() {
	if (addresses.size() + 2 == std::numeric_limits<uint16_t>::max())
	    return std::pair<Address, bool>(Address(), false);
	bool found = false;
	do {
	    if (last_proc_id + 1 == std::numeric_limits<uint16_t>::max())
		last_proc_id = 0;
	    if (addresses.find(Address(last_proc_id + 1, 0, segment_id))
		== addresses.end()) {
		found = true;
	    }
	} while (found == false);
	Address ret(++last_proc_id, 0, segment_id);
	if (addresses.insert(ret).second == false)
	    throw DException("");
	return std::pair<Address, bool>(ret, true);
    }

    void release_address(const Address addr) {
	// Erase all group memberships  
	MemberMap::iterator i, i_next;
	
	// Generate partitioning events for each member entry. Note that
	// this applies only to groups that were not left in the graceful
	// manner
	for (i = members.begin(); i != members.end(); i = i_next) {
	    i_next = i, ++i_next;
	    if (i->first.is_same_proc(addr) && i->first.is_same_segment(addr)) {
		GroupMap::iterator gi;
		if ((gi = groups.find(addr.get_service_id())) != groups.end() &&
		    gi->second.mi.find(i) != gi->second.mi.end()) {
		    partition(i->first);
		}
	    }
	}
	// Release address itself
	addresses.erase(addr);
    }
    
    
    
    bool join(const Address addr, Protolay *p) {
	if (members.find(addr) != members.end())
	    return false;
	deliver_conf(ConfMessage::JOIN, addr, p);
	return true;
    }
    
    bool leave(const Address addr) {
	if (members.find(addr) == members.end())
	    return false;
	deliver_conf(ConfMessage::LEAVE, addr, 0);
	return true;
    }

    void partition(const Address addr) {
	if (members.find(addr) == members.end())
	    return;
	deliver_conf(ConfMessage::PARTITION, addr, 0);
    }
    
    void deliver_empty_view(Member& m, const ServiceId sid) {
	// Create empty view message 
	VSView empty_view(true, VSViewId(0, Address(0, sid, 0)));
	VSMessage evm(sid, &empty_view);
	unsigned char *buf = new unsigned char[evm.size()];
	ReadBuf *rb = new ReadBuf(buf, evm.size());
	if (evm.write(buf, evm.size(), 0) == 0)
	    throw DException("");
	m.pl->handle_up(0, rb, 0, 0);
	rb->release();
	delete[] buf;
    }
    
    void deliver_msg() {
	ReadBuf *rb = msgs.pop_front();
	if (rb == 0)
	    throw DException("");
	VSMessage vmsg;
	if (vmsg.read(rb->get_buf(), rb->get_len(), 0) == 0)
	    throw DException("");
	
	if (vmsg.get_type() == VSMessage::CONF) {
	    const VSView& view(*vmsg.get_view());
	    // std::cerr << view << "\n";
	    if (view.is_trans()) {
		MemberMap::iterator i, i_next;
		for (i = members.begin(); i != members.end(); i = i_next) {
		    i_next = i, ++i_next;
		    if (i->first.is_same_service(vmsg.get_source())) {
			switch (i->second.state) {
			case Member::JOINING:
			case Member::CLOSED: // Can't trust protolay anymore
			    break;
			case Member::LEAVING:
			    deliver_empty_view(i->second, vmsg.get_source().get_service_id());
			    members.erase(i);
			    break;
			case Member::JOINED:
			    i->second.pl->handle_up(0, rb, 0, 0);
			    break;
			}
		    }
		}
	    } else {
		for (MemberMap::iterator i = members.begin();
		     i != members.end(); ++i) {
		    if (i->first.is_same_service(vmsg.get_source())) {
			// std::cerr << i->second.state << "\n";
			switch (i->second.state) {
			case Member::JOINING:
			    if (view.get_addr().find(i->first) != view.get_addr().end()) {
				deliver_empty_view(i->second, vmsg.get_source().get_service_id());
				i->second.pl->handle_up(0, rb, 0, 0);
				i->second.state = Member::JOINED;
			    }

			    break;
			case Member::JOINED:
			case Member::LEAVING:
			    i->second.pl->handle_up(0, rb, 0, 0);
			break;
			case Member::CLOSED: // Can't trust protolay anymore
			    break;
			}
		    }
		}
	    }
	} else {
	    for (MemberMap::iterator i = members.begin(); 
		 i != members.end(); ++i) {
		if (i->first.is_same_service(vmsg.get_source()) &&
		    i->second.state == Member::JOINED || 
		    i->second.state == Member::LEAVING)
		    i->second.pl->handle_up(0, rb, 0, 0);
	    }
	}
	rb->release();
    }
    
    Fifo::iterator find_last_conf_msg() {
	Fifo::iterator i;
	Fifo::iterator ret = msgs.end();
	for (i = msgs.begin(); i != msgs.end(); ++i) {
	    VSMessage vmsg;
	    if (vmsg.read((*i)->get_buf(), (*i)->get_len(), 0) == 0)
		throw DException("");
	    if (vmsg.get_type() == VSMessage::CONF)
		ret = i;
	}
	return ret;
    }

    
    void deliver_conf(ConfMessage::Type type, const Address addr, Protolay *p) {
	MemberMap::iterator mjoini, mi;
	GroupMap::iterator gi;
	VSView *trans_view = 0;
	VSView *reg_view = 0;
	if (type == ConfMessage::JOIN) {
	    std::pair<MemberMap::iterator, bool> mret;
	    mret = members.insert(std::pair<const Address, Member>(addr, Member(p)));
	    if (mret.second == false)
		throw DException("");
	    mi = mret.first;
	    
	    if ((gi = groups.find(addr.get_service_id())) == groups.end()) {
		std::pair<GroupMap::iterator, bool> gret;
		gret = groups.insert(std::pair<const ServiceId, Group>(addr.get_service_id(), Group()));
		if (gret.second == false)
		    throw DException("");
		gi = gret.first;
	    }
	    
	    VSViewId trans_view_id(gi->second.last_view_seq, 
				   gi->second.mi.empty() ? 
				   Address(0, addr.get_service_id(), 0) : 
				   (*gi->second.mi.begin())->first);

	    trans_view = new VSView(true, trans_view_id);
	    for (std::set<MemberMap::iterator>::iterator 
		     i = gi->second.mi.begin(); 
		 i != gi->second.mi.end(); ++i) {
		trans_view->addr_insert((*i)->first);
	    }
	    if (gi->second.mi.insert(mi).second == false)
		throw DException("");

	    VSViewId reg_view_id(++gi->second.last_view_seq, (*gi->second.mi.begin())->first);

	    reg_view = new VSView(false, reg_view_id);
	    for (std::set<MemberMap::iterator>::iterator 
		     i = gi->second.mi.begin(); 
		 i != gi->second.mi.end(); ++i) {
		reg_view->addr_insert((*i)->first);
	    }
	    reg_view->joined_insert(addr);
	} else if (type == ConfMessage::LEAVE || type == ConfMessage::PARTITION) {
	    
	    if ((mi = members.find(addr)) == members.end())
		throw DException("");
	    if ((gi = groups.find(addr.get_service_id())) == groups.end())
		throw DException("");
	    std::set<MemberMap::iterator>::iterator mmi;
	    if ((mmi = gi->second.mi.find(mi)) == gi->second.mi.end())
		throw DException("");
	    
	    gi->second.mi.erase(mmi);
	    if (type == ConfMessage::LEAVE)
		mi->second.state = Member::LEAVING;
	    else 
		mi->second.state = Member::CLOSED;
	    VSViewId trans_view_id(gi->second.last_view_seq, 
				   gi->second.mi.empty() ? 
				   Address(0, addr.get_service_id(), 0) : 
				   (*gi->second.mi.begin())->first);
	    
	    trans_view = new VSView(true, trans_view_id);
	    for (std::set<MemberMap::iterator>::iterator 
		     i = gi->second.mi.begin(); 
		 i != gi->second.mi.end(); ++i) {
		trans_view->addr_insert((*i)->first);
	    }
	    if (type == ConfMessage::LEAVE)
		trans_view->left_insert(addr);
	    else
		trans_view->partitioned_insert(addr);	    
	    if (gi->second.mi.empty() == false) {
		VSViewId reg_view_id(++gi->second.last_view_seq, 
				     (*gi->second.mi.begin())->first);
		reg_view = new VSView(false, reg_view_id);
		for (std::set<MemberMap::iterator>::iterator 
			 i = gi->second.mi.begin(); 
		     i != gi->second.mi.end(); ++i) {
		    reg_view->addr_insert((*i)->first);
		}
	    }
	} else {
	    throw DException("");
	}

	
	VSMessage trans_msg(addr.get_service_id(), trans_view);
	unsigned char *buf = new unsigned char[trans_msg.size()];
	if (trans_msg.write(buf, trans_msg.size(), 0) == 0)
	    throw DException("");

	WriteBuf trans_wb(buf, trans_msg.size());

	// Generate possibility to have transitional messages too...
	Fifo::iterator fi = find_last_conf_msg();
	int err;
	if (fi != msgs.end())
	    err = msgs.push_after(fi, &trans_wb);
	else 
	    err = msgs.push_front(&trans_wb);
	delete[] buf;
	if (err == EAGAIN)
	    throw DException("Fixme");
	else if (err != 0)
	    throw DException(strerror(errno));

	if (reg_view) {
	    VSMessage reg_msg(addr.get_service_id(), reg_view);
	    buf = new unsigned char[reg_msg.size()];
	    if (reg_msg.write(buf, reg_msg.size(), 0) == 0)
		throw DException("");
	    WriteBuf reg_wb(buf, reg_msg.size());
	    err = msgs.push_back(&reg_wb);
	    delete[] buf;
	    
	    if (err == EAGAIN)
		throw DException("Fixme");
	    else if (err != 0)
		throw DException(strerror(errno));
	}
	delete trans_view;
	delete reg_view;
    }

    int queue_message(WriteBuf *wb) {
	return msgs.push_back(wb);
    }

    void handle(const int fd, const PollEnum e) {
	// std::cerr << "VSFifoBackendProvider::handle()\n";
	if (e & PollEvent::POLL_IN) {
	    if (fd == msgs.get_read_fd()) {
		deliver_msg();
	    }  else {
		throw DException("Invalid fd");
	    }
	} 
	if (e & PollEvent::POLL_OUT) {
	    if (fd == msgs.get_write_fd()) {
		if (msgs.is_full())
		    deliver_msg();
	    } else {
		throw DException("");
	    }
	}
    }

};

static VSFifoBackendProvider *provider = 0;



VSFifoBackend::VSFifoBackend(Poll *p, Protolay *up_ctx)
{
    if (provider == 0)
	provider = new VSFifoBackendProvider(1, p);
    else if (p != provider->get_poll())
	throw DException("Invalid poll context");
    set_up_context(up_ctx);
}
 
VSFifoBackend::~VSFifoBackend()
{
    if (state == CONNECTED)
	close();
    if (provider && provider->is_empty()) {
	delete provider;
	provider = 0;
    }
}
    
void VSFifoBackend::handle_up(const int cid, const ReadBuf *rb, 
			      const size_t roff, const ProtoUpMeta *um)
{
    pass_up(rb, roff, 0);
}

int VSFifoBackend::handle_down(WriteBuf *wb, const ProtoDownMeta *dm)
{
    return provider->queue_message(wb);
}


void VSFifoBackend::connect(const char *be_addr)
{
    std::pair<Address, bool> ret = provider->alloc_address();
    if (ret.second == false)
	throw DException("");
    addr = ret.first;
    state = CONNECTED;
}
    

void VSFifoBackend::close()
{
    if (provider) {
	provider->release_address(addr);
    }
    state = CLOSED;
}
    
void VSFifoBackend::join(const ServiceId sid)
{
    if (state != CONNECTED)
	throw DException("Not connected");
    Address a(addr.get_proc_id(), sid, addr.get_segment_id());
    if (provider->join(a, this) == false)
	throw DException("");
}
    
void VSFifoBackend::leave(const ServiceId sid)
{
    if (state != CONNECTED)
	throw DException("Not connected");
    Address a(addr.get_proc_id(), sid, addr.get_segment_id());
    if (provider->leave(a) == false)
	throw DException("");
}
