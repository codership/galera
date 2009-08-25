#ifndef VS_HPP
#define VS_HPP

#include <galeracomm/protolay.hpp>
#include <galeracomm/poll.hpp>
#include <galeracomm/address.hpp>
#include <galeracomm/exception.hpp>
#include <galeracomm/monitor.hpp>

#include <set>
#include <map>
#include <deque>

#include <cerrno>
#include <iostream>

class VSViewId {
    uint32_t seq;
    Address repr;
public:
    VSViewId() : seq(0), repr(ADDRESS_INVALID) {}
    VSViewId(const uint32_t s, const Address& r) : seq(s), repr(r) {}

    uint32_t get_seq() const {
	return seq;
    }
    Address get_repr() const {
	return repr;
    }

    // Relations
    bool operator<(const VSViewId& vid) const {
	return seq < vid.seq || (seq == vid.seq && repr < vid.repr);
    }
    bool operator==(const VSViewId& vid) const {
	return seq == vid.seq && repr == vid.repr;
    }
    bool operator!=(const VSViewId vid) const {
	return !(*this == vid);
    }
    // Serialization
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	size_t off = 0;
	if ((off = read_uint32(buf, buflen, offset, &seq)) == 0)
	    return 0;
	if ((off = repr.read(buf, buflen, off)) == 0)
	    return 0;
	return off;
    }
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	size_t off;
	if ((off = write_uint32(seq, buf, buflen, offset)) == 0)
	    return 0;
	if ((off = repr.write(buf, buflen, off)) == 0)
	    return 0;
	return off;
    }
    size_t size() const {
	return 4 + repr.size();
    }

};

inline std::ostream& operator<<(std::ostream& ost, const VSViewId& vid)
{
    ost << "ViewId(" << vid.get_seq() << "," << vid.get_repr() << ")";
    return ost;
}


static inline size_t read_aset(const void *buf, const size_t buflen, 
			       const size_t offset, std::set<Address>& addr)
{
    size_t off;
    uint32_t s;
    Address a;

    if ((off = read_uint32(buf, buflen, offset, &s)) == 0)
	return 0;
    for (size_t i = 0; i < s; i++) {
	if ((off = a.read(buf, buflen, off)) == 0)
	    return 0;
	addr.insert(a);
    }
    return off;
}

static inline size_t write_aset(const std::set<Address>& addr, void *buf,
				const size_t buflen, const size_t offset)
{
    size_t off;
    uint32_t s;
    s = addr.size();
    if ((off = write_uint32(s, buf, buflen, offset)) == 0)
	return 0;
    for (std::set<Address>::const_iterator i = addr.begin();
	 i != addr.end(); ++i) {
	if ((off = i->write(buf, buflen, off)) == 0)
	    return 0;
    }
    return off;
}

static inline size_t size_aset(const std::set<Address>&addr)
{
    return 4 + addr.size()*ADDRESS_INVALID.size();
}

class VSView {
    bool trans;
    VSViewId vid;

    std::set<Address> addr;
    std::set<Address> joined;
    std::set<Address> left;
    std::set<Address> partitioned;

public:
    VSView() :
        trans(0), vid(),
        addr(), joined(), left(), partitioned()
    {}

    VSView(const bool it, VSViewId v) :
	trans(it), vid(v),
        addr(), joined(), left(), partitioned()
    {}

    ~VSView() {}
/*
  bool operator==(const VSView& a) const {
  return trans == a.trans && vid == a.vid &&
  addr == a.addr && joined == a.joined && 
  left == a.left && partitioned == a.partitioned;
  }
*/
    void addr_insert(const Address& a) {
	if (left.size() && left.find(a) != left.end())
	    throw DException("");
	if (partitioned.size() && partitioned.find(a) != partitioned.end())
	    throw DException("");
        if (addr.insert(a).second == false)
	    throw DException("");
    }

    void addr_insert(std::set<Address>::iterator begin,
		     std::set<Address>::iterator end) {
	while (begin != end) {
	    addr_insert(*begin);
	    ++begin;
	}
    }

    void joined_insert(const Address& a) {
	if (left.size() && left.find(a) != left.end())
	    throw DException("");
	if (partitioned.size() && partitioned.find(a) != partitioned.end())
	    throw DException("");
	if ((addr.size() && addr.find(a) != addr.end()) == false)
	    throw DException("");
	if (joined.insert(a).second == false)
	    throw DException("");
    }

    void left_insert(const Address& a) {
	if (addr.size() && addr.find(a) != addr.end())
	    throw DException("");
	if (joined.size() && joined.find(a) != joined.end())
	    throw DException("");
	if (partitioned.size() && partitioned.find(a) != partitioned.end())
	    throw DException("");
	if (left.insert(a).second == false)
	    throw DException("");
    }

    void partitioned_insert(const Address& a) {
	if (addr.size() && addr.find(a) != addr.end())
	    throw DException("");
	if (joined.size() && joined.find(a) != joined.end())
	    throw DException("");
	if (left.size() && left.find(a) != left.end())
	    throw DException("");
        if (partitioned.insert(a).second == false)
	    throw DException("");
    }

    const std::set<Address>& get_addr() const {
	return addr;
    }

    const std::set<Address>& get_joined() const {
	return joined;
    }

    const std::set<Address>& get_left() const {
	return left;
    }

    const std::set<Address>& get_partitioned() const {
	return partitioned;
    }


    
    bool is_trans() const {
	return trans;
    }

    const VSViewId& get_view_id() const {
	return vid;
    }

    // Serialization
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	uint32_t w;
	size_t off;
	if ((off = read_uint32(buf, buflen, offset, &w)) == 0)
	    return 0;
	trans = w & 0xff;
	if ((off = vid.read(buf, buflen, off)) == 0)
	    return 0;
	if ((off = read_aset(buf, buflen, off, addr)) == 0)
	    return 0;
	if ((off = read_aset(buf, buflen, off, joined)) == 0)
	    return 0;
	if ((off = read_aset(buf, buflen, off, left)) == 0)
	    return 0;
	if ((off = read_aset(buf, buflen, off, partitioned)) == 0)
	    return 0;
	return off;
    }
    
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	uint32_t w = trans ? 0x1 : 0;
	size_t off;
	if ((off = write_uint32(w, buf, buflen, offset)) == 0)
	    return 0;
	if ((off = vid.write(buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_aset(addr, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_aset(joined, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_aset(left, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_aset(partitioned, buf, buflen, off)) == 0)
	    return 0;
	return off;
    }
    size_t size() const {
	return 4 + vid.size() + size_aset(addr) + size_aset(joined) + size_aset(left) + size_aset(partitioned);
    }    
    
};

inline bool operator==(const VSView& a, const VSView& b)
{
    return a.is_trans() == b.is_trans() &&
	a.get_view_id() == b.get_view_id() &&
	a.get_addr() == b.get_addr();
}

inline std::ostream& operator<<(std::ostream& os, const VSView& view)
{
    os << "View(" << (view.is_trans() ? "Trans," : "Reg,");
    os << view.get_view_id() << "): ";
    for (std::set<Address>::iterator i = view.get_addr().begin(); i != view.get_addr().end();
	 ++i)
	os << *i << " ";
    return os;
}



class VSMessage {
    uint8_t version;
    uint8_t type;
    uint8_t user_type;
    mutable uint8_t flags; // Ugly, but for cksumming
    VSView *view;
    Address source;
    VSViewId source_view;
    Address destination;
    uint32_t seq;
    mutable uint32_t cksum;
    size_t data_offset;
    const Serializable *user_state;
    ReadBuf *user_state_buf;
    size_t hdrlen;
public:
    enum Type {NONE, CONF, STATE, DATA};
    enum {
	F_USER_STATE = 1 << 0, // Message contains user state (state msg)
	F_CKSUM      = 1 << 1, // Message contains header checksum
	F_CAUSAL     = 1 << 2, // Message may be delivered using causal cons
	F_AGREED     = 1 << 3  // Message may be delivered using agreed cons
    };

    VSMessage() :
        version(1), 
        type(NONE), 
        user_type(0),
        flags(0),
        view(0),
        source(Address(0, 0, 0)),
        source_view(),
        destination(Address(0, 0, 0)),
        seq(0),
        cksum(0),
        data_offset(0),
        user_state(0),
        user_state_buf(0),
        hdrlen(0)
    {}

    VSMessage& operator= (const VSMessage& m)
    {
	if (view)           delete view;
	if (user_state_buf) user_state_buf->release();

        *this = m;

	if (m.view)           view = new VSView(*m.view);
	if (m.user_state_buf) user_state_buf = m.user_state_buf->copy();

        return *this;
    };

    // Ctor for conf message
    VSMessage(const ServiceId sid, const VSView *v) : 
	version(1),
	type(CONF), 
	user_type(0),
	flags(0),
	view(0),
	source(Address(0, sid, 0)),
        source_view(),
	destination(Address(0, 0, 0)),
	seq(0),
        cksum(0),
	data_offset(0),
        user_state(0),
        user_state_buf(0),
        hdrlen (write_hdr(hdrbuf, sizeof(hdrbuf), 0))
    {
	if (!v) throw DException("");
	if (hdrlen == 0) throw DException("");
	view = new VSView(*v);
    }
    
    // Ctor for state message
    VSMessage(const Address& s, const VSViewId& sv, const VSView *v,
	      const uint32_t last_seq, const Serializable *us) :
	version(1),
	type(STATE), 
	user_type(0),
	flags(0),
	view(0),
	source(s), 
	source_view(sv),
	destination(Address(0, 0, 0)),
	seq(last_seq),
        cksum(0),
	data_offset(0),
	user_state(us),
	user_state_buf(0),
        hdrlen (write_hdr(hdrbuf, sizeof(hdrbuf), 0))
    {
	if (type != STATE)
	    throw DException("");
	if (!v)
	    throw DException("");
	if (source.get_service_id() != ServiceId(0))
	    throw DException("");
	if (hdrlen == 0)
	    throw DException("");

	view = new VSView(*v);

	flags |= user_state ? F_USER_STATE : 0;
    }

    // Ctor for broadcast data message
    VSMessage(const Address& s, const VSViewId& sv,
	      const uint32_t sq, const uint8_t ut = 0) : 
	version(1),
	type(DATA), 
	user_type(ut),
	flags(0),
	view(0),
	source(s), 
	source_view(sv),
	destination(0, s.get_service_id(), 0),
	seq(sq),
        cksum(0),
	data_offset(0),
	user_state(0),
	user_state_buf(0),
        hdrlen (write_hdr(hdrbuf, sizeof(hdrbuf), 0))
    {
	if (type != DATA)
	    throw DException("");
	if (hdrlen == 0)
	    throw DException("");
    }
    
    // Ctor for data message with given destination
    VSMessage(const Address& s, const VSViewId& sv,
	      const Address& d, const uint32_t sq) : 
	version(1),
	type(DATA), 
	user_type(0),
	flags(0),
	view(0),
	source(s), 
	source_view(sv),
	destination(d),
	seq(sq),
        cksum(0),
	data_offset(0),
	user_state(0),
	user_state_buf(0),
        hdrlen (write_hdr(hdrbuf, sizeof(hdrbuf), 0))
    {
	if (type != DATA)
	    throw DException("");
	if (hdrlen == 0)
	    throw DException("");
    }

    // Copy constructor
    VSMessage(const VSMessage& m) :
	version(m.version),
	type(m.type), 
	user_type(m.user_type),
	flags(m.flags),
	view(0),
	source(m.source), 
	source_view(m.source_view),
	destination(m.destination),
	seq(m.seq),
        cksum(m.cksum),
	data_offset(m.data_offset),
	user_state(m.user_state),
	user_state_buf(0),
        hdrlen(m.hdrlen)
    {
	*this = m;
	if (m.view)
	    this->view = new VSView(*m.view);
	if (m.user_state_buf)
	    this->user_state_buf = m.user_state_buf->copy();
    }

    ~VSMessage() {
	delete view;
	if (user_state_buf)
	    user_state_buf->release();
    }
  

    int get_version() const {
	return version;
    }

    int get_type() const {
	return type;
    }

    const VSView *get_view() const {
	return view;
    }

    const Address& get_source() const {
	return source;
    }

    const VSViewId& get_source_view() const {
	return source_view;
    }
    const Address& get_destination() const {
	return destination;
    }
    uint32_t get_seq() const {
	return seq;
    }

    size_t get_data_offset() const {
	return data_offset;
    }

    const ReadBuf *get_user_state_buf() const {
	return user_state_buf;
    }
    uint8_t get_user_type() const {
	return user_type;
    }

    // Serialization
    
    const void *get_hdr() const {
	return hdrbuf;
    }
    size_t get_hdrlen() const {
	return hdrlen;
    }
private:
    unsigned char hdrbuf[4 + 4 + 8 + 4 + 4];
    void do_cksum(const void *buf, const size_t buflen, uint32_t *ret) const {
	const unsigned char *ptr = reinterpret_cast<const unsigned char *>(buf);
	*ret = 0;
	while (ptr < buflen +  reinterpret_cast<const unsigned char *>(buf))
	    *ret += *ptr++, cksum *= 0xabc175db;
    }

    size_t write_hdr(void *buf, const size_t buflen, const size_t offset) const {
	uint32_t w;
	size_t off;
	
	flags |= F_CKSUM; // We do cksum for now on
	
	w = user_type, w <<= 8;
	w |= flags, w <<= 8;
	w |= type & 0xff, w <<= 8;
	w |= version & 0xff;
	
	
	if ((off = write_uint32(w, buf, buflen, offset)) == 0)
	    return 0;
	if ((off = source.write(buf, buflen, off)) == 0)
	    return 0;
	if ((off = source_view.write(buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_uint32(seq, buf, buflen, off)) == 0)
	    return 0;
	
	do_cksum(reinterpret_cast<const unsigned char *>(buf) + offset, 
		 4 + 4 + 8 + 4, &cksum);
	return write_uint32(cksum, buf, buflen, off);
    }
public:
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	uint32_t w;
	size_t off;

	// LOG_TRACE(std::string("VSMessage::read(): reading "
	//				     "vtf, offset ") 
	//			 + to_string(offset));
	if ((off = read_uint32(buf, buflen, offset, &w)) == 0) {
	    LOG_WARN("VSMessage::read() failed at hdr read");
	    return 0;
	}
	version = w & 0xff;
	type = (w >> 8) & 0xff;
	flags = (w >> 16) & 0xff;
	user_type = (w >> 24) & 0xff;

	// LOG_TRACE(std::string("VSMessage::read(): reading "
	//				     "source, offset ") 
	//			 + to_string(off));
	if ((off = source.read(buf, buflen, off)) == 0) {
	    LOG_WARN("VSMessage::read() failed at source read");
	    return 0;
	}

	// LOG_TRACE(std::string("VSMessage::read(): reading "
	//				     "source view, offset ") 
	//			 + to_string(off));
	if ((off = source_view.read(buf, buflen, off)) == 0) {
	    LOG_WARN("VSMessage::read() failed at view id read");
	    return 0;
	}

	// LOG_TRACE(std::string("VSMessage::read(): reading "
	//				     "seq, offset ") 
	//			 + to_string(off));
	if ((off = read_uint32(buf, buflen, off, &seq)) == 0) {
	    LOG_WARN("VSMessage::read() failed at seq read");
	    return 0;
	}
	
	// Compute checksum from header read so far and compare it to 
	// checksum found from buffer
	do_cksum(reinterpret_cast<const unsigned char *>(buf) + offset, 
		 off - offset, &cksum);
	uint32_t ck;
	if ((off = read_uint32(buf, buflen, off, &ck)) == 0) {
	    LOG_WARN("VSMessage::read() failed at cksum read");
	    return 0;
	}
	if (ck != cksum) {
	    throw FatalException("Invalid cksum");
	}
	
	hdrlen = off;
	
	switch (type) {
	case CONF:
	case STATE:
	    view = new VSView();
	    if ((off = view->read(buf, buflen, off)) == 0) {
		LOG_WARN("VSMessage::read() failed at view read");
		return 0;
	    }
	    if (flags & F_USER_STATE) {
		user_state_buf =
                    new ReadBuf(reinterpret_cast<const char*>(buf) + off,
                                buflen - off);
	    }
	    break;
	case DATA:
	    data_offset = off;
	    break;
	}
	

//	LOG_TRACE(std::string("VSMessage::read(): returning, "
//					     "offset ") 
//				 + to_string(off));
	return off;
    }

    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	size_t off = write_hdr(buf, buflen, offset);
	if (off == 0)
	    return 0;
	switch (type) {
	case CONF:
	case STATE:
	    if ((off = view->write(buf, buflen, off)) == 0)
		return 0;
	    if (flags & F_USER_STATE) {
		assert(user_state);
		if ((off = user_state->write(buf, buflen, off)) == 0)
		    return 0;
	    } else {
		assert(user_state == 0);
	    }
	    break;
	}
	return off;
    }
    
    size_t size() const {
	if (type == NONE)
	    throw DException("");
	return 
	    4                           // Version + type + flags
	    + source.size()             // Source address
	    + source_view.size()        // View identifier
	    + 4                         // Seqno
	    + 4                         // Cksum
	    + (view ? view->size() : 0) // View (state msg)
	    + (user_state ? 
	       user_state->size() : 0); // User state (state msg)
    }
  
};

static inline bool operator==(const VSMessage& a, const VSMessage& b)
{
    return a.get_version() == b.get_version() && 
	a.get_type() == b.get_type() &&
	a.get_source() == b.get_source() &&
	a.get_source_view() == b.get_source_view() &&
	a.get_destination() == b.get_destination() &&
	a.get_seq() == b.get_seq();
}


class VSBackend;



class VSMemb {
    VSMemb (const VSMemb&);
    void operator= (const VSMemb&);
public:
    Address addr;
    uint32_t expected_seq;
    VSMessage *state_msg;
    VSMemb(const Address& a) : addr(a), expected_seq(0), state_msg(0) {}
    ~VSMemb() {
	delete state_msg;
    }
};

typedef std::pair<VSView *, VSView *> VSViewMap;
typedef std::map<const Address, VSMemb *> VSMembMap;
typedef std::map<const Address, const ReadBuf *> VSUserStateMap;

struct VSDownMeta : ProtoDownMeta {
    ServiceId sid;
    uint8_t user_type;
    VSDownMeta(const ServiceId& s, const uint8_t ut) : sid(s), user_type(ut) {}
};



struct VSUpMeta : ProtoUpMeta {
    const VSView *view;
    const VSUserStateMap *state_map;
    const VSMessage *msg;
    VSUpMeta() : view(0), state_map(0), msg(0) {}
    VSUpMeta(const VSView *v, const VSUserStateMap *sm) : 
	view(v), state_map(sm), msg(0) {}
    VSUpMeta(const VSMessage *m) : view(0), state_map(0), msg(m) {}

private:

    VSUpMeta (const VSUpMeta&);
    void operator= (const VSUpMeta&);
};


/*
 * Note: Protolay bindings
 * -> Down
 * <- Up
 * 
 * User -> VS 
 * VSProto -> VSBackend
 *
 * VSBackend -> VS
 * VSProto -> User
 *
 */

class VSProto;

typedef std::map<const ServiceId, VSProto *> VSProtoMap;

class VS : public Protolay {
    VSBackend *be;
    char *be_addr;
    VSProtoMap proto;
    Monitor *mon;

    VS (const VS&);
    void operator= (const VS&);

public:

    int handle_down(WriteBuf *, const ProtoDownMeta *);
    void handle_up(const int, const ReadBuf *, const size_t, const ProtoUpMeta *);
    Address get_self() const;
    Address get_self(const ServiceId) const;
    
    
    VS();
    VS(Monitor *m);
    ~VS();
    
    // Connect to backend
    void connect();
    // Close backend
    void close();
    // Join to service
    void join(const ServiceId, Protolay *, const Serializable *user_state = 0);
    // Leave service
    void leave(const ServiceId);
    
    static VS *create(const char *, Poll *, Monitor *);
};

#endif // VS_HPP
