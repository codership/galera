#ifndef EVS_MESSAGE_H
#define EVS_MESSAGE_H

#include <cstdlib>

#include "gcomm/exception.hpp"
#include "gcomm/address.hpp"
#include "gcomm/logger.hpp"

#include "evs_seqno.hpp"

#include <map>

typedef Address EVSPid;

class EVSViewId {
    uint8_t uuid[8];
public:
    EVSViewId() {
	memset(uuid, 0, 8);
    }
    EVSViewId(const EVSPid& sa, const uint32_t seq) {
	sa.write(uuid, 4, 0);
	write_uint32(seq, uuid, sizeof(uuid), 4);
    }

    uint32_t get_seq() const {
	uint32_t ret;
	if (read_uint32(uuid, sizeof(uuid), 4, &ret) == 0)
	    throw FatalException("");
	return ret;
    }

    EVSPid get_pid() const {
	Address sa;
	sa.read(uuid, 4, 0);
	return sa;
    }

    size_t read(const void* buf, const size_t buflen, const size_t offset) {
	if (offset + 8 > buflen)
	    return 0;
	memcpy(uuid, (const uint8_t*)buf + offset, 8);
	return offset + 8;
    }

    size_t write(void* buf, const size_t buflen, const size_t offset) const {
	if (offset + 8 > buflen)
	    return 0;
	memcpy((uint8_t*)buf + offset, uuid, 8);
	return offset + 8;
    }

    static size_t size() {
	return 8;
    }

    bool operator<(const EVSViewId& cmp) const {
	return memcmp(uuid, cmp.uuid, 8) < 0;
    }
    bool operator==(const EVSViewId& cmp) const {
	return memcmp(uuid, cmp.uuid, 8) == 0;
    }

    std::string to_string() const {
	return get_pid().to_string() + ":" + ::to_string(get_seq());
    }

};


struct EVSRange {
    uint32_t low;
    uint32_t high;
    EVSRange() : low(SEQNO_MAX), high(SEQNO_MAX) {}
    EVSRange(const uint32_t low_, const uint32_t high_) :
	low(low_), high(high_) {}
    uint32_t get_low() const {
	return low;
    }
    uint32_t get_high() const {
	return high;
    }
    bool operator==(const EVSRange& cmp) const {
	return cmp.get_low() == low && cmp.get_high() == high;
    }
    std::string to_string() const {
        return std::string("[") + ::to_string(low) + "," + ::to_string(high) + "]";
    }
};

struct EVSGap {
    EVSPid source;
    EVSRange range;
    EVSGap() {}
    EVSGap(const EVSPid& source_, const EVSRange& range_) :
	source(source_), range(range_) {}

    EVSPid get_source() const {
	return source;
    }
    uint32_t get_low() const {
	return range.low;
    }

    uint32_t get_high() const {
	return range.high;
    }

    size_t read(const void* buf, const size_t buflen, const size_t offset) {
        size_t off;
        if ((off = source.read(buf, buflen, offset)) == 0)
            return 0;
        if ((off = read_uint32(buf, buflen, off, &range.low)) == 0)
            return 0;
        if ((off = read_uint32(buf, buflen, off, &range.high)) == 0)
            return 0;
        return off;
    }

    size_t write(void* buf, const size_t buflen, const size_t offset) const {
        size_t off;
        if ((off = source.write(buf, buflen, offset)) == 0)
            return 0;
        if ((off = write_uint32(range.low, buf, buflen, off)) == 0)
            return 0;
        if ((off = write_uint32(range.high, buf, buflen, off)) == 0)
            return 0;
        return off;
    }

    size_t size() const {
        return source.size() + 8;
    }


};

enum EVSSafetyPrefix {
    DROP,
    UNRELIABLE,
    FIFO,
    AGREED,
    SAFE
};



class EVSMessage {
public:
    
    enum Type {
	USER,     // User message
	DELEGATE, // Message that has been sent on behalf of other instace 
	GAP,      // Message containing seqno arrays and/or gaps
	JOIN,     // Join message
	LEAVE,    // Leave message
	INSTALL   // Install message
    };

    static std::string to_string(const Type t) {
        switch (t) {
        case USER:
            return "USER";
        case DELEGATE:
            return "DELEGATE";
        case GAP:
            return "GAP";
        case JOIN:
            return "JOIN";
        case LEAVE:
            return "LEAVE";
        case INSTALL:
            return "INSTALL";
        }
        return "";
    }


    enum Flag {
	F_MSG_MORE = 0x1,
        F_RESEND = 0x2
    };

private:
    int version;
    Type type;
    EVSSafetyPrefix safety_prefix;
    uint32_t seq;
    uint8_t seq_range;
    uint32_t aru_seq;
    uint8_t flags;
    EVSViewId source_view;
    EVSPid source;
    EVSGap gap;
public:
    class Instance {
	EVSPid pid;
	bool operational;
	bool trusted;
	EVSViewId view_id;
	EVSRange range;
    public:
	Instance() {}
	Instance(const EVSPid pid_, const bool oper_, const bool trusted_, 
		 const EVSViewId& view_id_,
		 const EVSRange range_) :
	    pid(pid_), operational(oper_), trusted(trusted_), 
	    view_id(view_id_), range(range_) {}
	const EVSPid& get_pid() const {
	    return pid;
	}
	bool get_operational() const {
	    return operational;
	}
	bool get_trusted() const {
	    return trusted;
	}
	const EVSViewId& get_view_id() const {
	    return view_id;
	}
	const EVSRange& get_range() const {
	    return range;
	}
	
	size_t write(void* buf, 
		     const size_t buflen, const size_t offset) {
	    size_t off;
	    uint32_t b = (operational ? 0x1 : 0x0) | (trusted ? 0x10 : 0x0);
	    if ((off = write_uint32(b, buf, buflen, offset)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = pid.write(buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = view_id.write(buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = write_uint32(range.get_low(), buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = write_uint32(range.get_high(), buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    return off;
	}
	
	size_t read(const void* buf, 
                    const size_t buflen, const size_t offset) {
	    size_t off;
	    uint32_t b;
	    if ((off = read_uint32(buf, buflen, offset, &b)) == 0)
		return 0;
	    operational = b & 0x1;
	    trusted = b & 0x10;
	    if ((off = pid.read(buf, buflen, off)) == 0)
		return 0;
	    if ((off = view_id.read(buf, buflen, off)) == 0)
		return 0;
	    uint32_t low, high;
	    if ((off = read_uint32(buf, buflen, off, &low)) == 0)
		return 0;
	    if ((off = read_uint32(buf, buflen, off, &high)) == 0)
		return 0;
	    range = EVSRange(low, high);
	    return off;
	}

	static size_t size() {
	    return 4 + 4 + EVSViewId::size() + 4 + 4;
	}
    };
private:
    std::map<EVSPid, Instance>* instances;
public:    
    
    
    EVSMessage() : seq(SEQNO_MAX), instances(0) {}

    EVSMessage(const EVSMessage& m) {
	*this = m;
	if (m.instances) {
	    instances = new std::map<EVSPid, Instance>();
	    *instances = *m.instances;
	}
    }
    
    // User message
    EVSMessage(const Type type_, 
	       const EVSPid& source_,
	       const EVSSafetyPrefix sp_, 
	       const uint32_t seq_, 
	       const uint8_t seq_range_,
	       const uint32_t aru_seq_,
	       const EVSViewId vid_, const uint8_t flags_) : 
	version(0), 
	type(type_), 
	safety_prefix(sp_), 
	seq(seq_), 
	seq_range(seq_range_),
	aru_seq(aru_seq_),
	flags(flags_),
        source_view(vid_), 
	source(source_),
	instances(0) {
	if (type != USER)
	    throw FatalException("Invalid type");
    }
    
    // Delegate message
    EVSMessage(const Type type_, const EVSPid& source_) :
	version(0), 
	type(type_), 
	source(source_), 
	instances(0) {
	if (type != DELEGATE)
	    throw FatalException("Invalid type");
    }
    
    // Gap message
    EVSMessage(const Type type_, const EVSPid& source_, 
	       const EVSViewId& source_view_, 
	       const uint32_t seq_, const uint32_t aru_seq_, const EVSGap& gap_) :
        version(0), 
        type(type_), 
        seq(seq_),
        seq_range(0),
        aru_seq(aru_seq_),
        source_view(source_view_),
        source(source_),
        gap(gap_),
        instances(0)
        {
            if (type != GAP)
	        throw FatalException("Invalid type");
        } 

    // Join and install messages
    EVSMessage(const Type type_, const EVSPid& source_, 
	       const EVSViewId& vid_, 
	       const uint32_t aru_seq_, const uint32_t safe_seq_) :
	version(0),
	type(type_),
	seq(safe_seq_),
        seq_range(0),
	aru_seq(aru_seq_),
	source_view(vid_),
	source(source_),
        instances(0) {
	
	if (type != JOIN && type != INSTALL && type != LEAVE)
	    throw FatalException("Invalid type");
        if (type != LEAVE)
            instances = new std::map<EVSPid, Instance>();
    }


	
    
    ~EVSMessage() {
	delete instances;
    }
    
    Type get_type() const {
	return type;
    }
    
    EVSSafetyPrefix get_safety_prefix() const {
	return safety_prefix;
    }
    
    EVSPid get_source() const {
	return source;
    }

    uint32_t get_seq() const {
	return seq;
    }
    
    uint8_t get_seq_range() const {
	return seq_range;
    }

    uint32_t get_aru_seq() const {
	return aru_seq;
    }

    uint8_t get_flags() const {
	return flags;
    }
    
    EVSViewId get_source_view() const {
	return source_view;
    }

    EVSGap get_gap() const {
	return gap;
    }

    const std::map<EVSPid, Instance>* get_instances() const {
	return instances;
    }
    
    void add_instance(const EVSPid& pid, const bool operational, 
		      const bool trusted, 
		      const EVSViewId& view_id, 
		      const EVSRange& range) {
	std::pair<std::map<EVSPid, Instance>::iterator, bool> i = 
	    instances->insert(std::pair<EVSPid, Instance>(
				  pid, 
				  Instance(pid, operational, trusted, 
					   view_id, range)));
	if (i.second == false)
	    throw FatalException("");
    }
    
    
    // Message serialization:

    size_t read(const void* buf, const size_t buflen, const size_t offset) {
	uint8_t b;
	size_t off;
	if ((off = read_uint8(buf, buflen, offset, &b)) == 0)
	    return 0;
	version = b & 0xf;
	type = static_cast<Type>((b >> 4) & 0xf);
	
	if ((off = read_uint8(buf, buflen, off, &b)) == 0)
	    return 0;
	safety_prefix = static_cast<EVSSafetyPrefix>(b & 0xf);
	if ((off = read_uint8(buf, buflen, off, &seq_range)) == 0)
	    return 0;
	if ((off = read_uint8(buf, buflen, off, &flags)) == 0)
	    return 0;
	if ((off = source.read(buf, buflen, off)) == 0)
	    return 0;

	if (type == USER || type == JOIN || type == INSTALL || type == LEAVE || type == GAP) {
	    if ((off = read_uint32(buf, buflen, off, &seq)) == 0)
		return 0;
	    if ((off = read_uint32(buf, buflen, off, &aru_seq)) == 0)
		return 0;
	    if ((off = source_view.read(buf, buflen, off)) == 0)
		return 0;
	    if (type == JOIN || type == INSTALL) {
		uint32_t n;
		if ((off = read_uint32(buf, buflen, off, &n)) == 0)
		    return 0;
		delete instances;
		instances = new std::map<EVSPid, Instance>();
		for (size_t i = 0; i < n; ++i) {
		    Instance inst;
		    if ((off = inst.read(buf, buflen, off)) == 0)
			return 0;
		    std::pair<std::map<EVSPid, Instance>::iterator, bool> ii = 		    
			instances->insert(std::pair<EVSPid, Instance>(inst.get_pid(), inst));
		    if (ii.second  == false)
			return 0;
		}
	    } else if (type == GAP) {
                if ((off = gap.read(buf, buflen, off)) == 0)
                    return 0;
            }
	}
	return off;
    }
    
    size_t write(void* buf, const size_t buflen, const size_t offset) const {
	uint8_t b;
	size_t off;
	
	/* Common header for all messages */
	/* Version, type */
	b = (version & 0xf) | ((type << 4) & 0xf0);
	if ((off = write_uint8(b, buf, buflen, offset)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Safety prefix */
	b = safety_prefix & 0xf;	
	if ((off = write_uint8(b, buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Seq range */
	if ((off = write_uint8(seq_range, buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Flags */
	if ((off = write_uint8(flags, buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Message source pid */
	if ((off = source.write(buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	
	if (type == USER || type == JOIN || type == INSTALL || type == LEAVE ||
	    type == GAP) {
	    if ((off = write_uint32(seq, buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = write_uint32(aru_seq, buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = source_view.write(buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if (type == JOIN || type == INSTALL) {
		if ((off = write_uint32(instances->size(), buf, buflen, off)) == 0) {
		    LOG_TRACE("");
		    return 0;
		}
		for (std::map<EVSPid, Instance>::iterator i = instances->begin(); i != instances->end(); ++i) {
		    if ((off = i->second.write(buf, buflen, off)) == 0) {
			LOG_TRACE("");
			return 0;
		    }
		}
	    } else if (type == GAP) {
                if ((off = gap.write(buf, buflen, off)) == 0)
                    return 0;
            }
	} 
	return off;
    }
    
    size_t size() const {
	switch (type) {
	case USER:
	    return 4 + 4 + 4 + source.size() + source_view.size(); // bits + seq + aru_seq + view
	case GAP:
	    return 4 + 4 + 4 + source.size() + source_view.size() + gap.size(); // bits + seq + aru_seq + view + gap
	case DELEGATE:
	    return 4 + source.size(); // 
	case JOIN:
	case INSTALL:
	    return 4 + 4 + 4 + source.size() + source_view.size() + 4 + instances->size()*Instance::size();
	case LEAVE:
	    return 4 + 4 + 4 + source.size() + source_view.size();
	}
	return 0;
    }

    mutable unsigned char hdrbuf[32];
    const void* get_hdr() const {
	if (write(hdrbuf, sizeof(hdrbuf), 0) == 0)
	    throw FatalException("Short buffer");
	return hdrbuf;
    }

    size_t get_hdrlen() const {
	return size();
    }
    
};

// Compare two evs messages
inline bool equal(const EVSMessage* a, const EVSMessage* b)
{
    if (a->get_type() != b->get_type())
	return false;
    switch (a->get_type()) {

    case EVSMessage::JOIN:

    default:
	LOG_DEBUG(std::string("equal() not implemented for ") + ::to_string(a->get_type()));
    }
    return false;
}

#endif // EVS_MESSAGE
