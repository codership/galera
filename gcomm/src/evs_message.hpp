#ifndef EVS_MESSAGE_H
#define EVS_MESSAGE_H



#include "gcomm/common.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/view.hpp"
#include "evs_seqno.hpp"

#include <map>
#include <utility>
#include <cstdlib>

using std::map;
using std::make_pair;
using std::string;


BEGIN_GCOMM_NAMESPACE

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
        return std::string("[") + UInt32(low).to_string() 
            + "," + UInt32(high).to_string() + "]";
    }
};

struct EVSGap {
    UUID source;
    EVSRange range;
    EVSGap() {}
    EVSGap(const UUID& source_, const EVSRange& range_) :
	source(source_), range(range_) {}

    UUID get_source() const {
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
        if ((off = gcomm::read(buf, buflen, off, &range.low)) == 0)
            return 0;
        if ((off = gcomm::read(buf, buflen, off, &range.high)) == 0)
            return 0;
        return off;
    }

    size_t write(void* buf, const size_t buflen, const size_t offset) const {
        size_t off;
        if ((off = source.write(buf, buflen, offset)) == 0)
            return 0;
        if ((off = gcomm::write(range.low, buf, buflen, off)) == 0)
            return 0;
        if ((off = gcomm::write(range.high, buf, buflen, off)) == 0)
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
        NONE,
	USER,     // User message
	DELEGATE, // Message that has been sent on behalf of other instace 
	GAP,      // Message containing seqno arrays and/or gaps
	JOIN,     // Join message
	LEAVE,    // Leave message
	INSTALL   // Install message
    };


    static std::string to_string(const Type t) {
        switch (t) {
        case NONE:
            return "NONE";
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
    ViewId source_view;
    UUID source;
    char name[16];
    EVSGap gap;
public:
    class Instance {
	UUID pid;
        char name[16];
	bool operational;
	bool trusted;
        bool left;
	ViewId view_id;
	EVSRange range;
    public:
	Instance() {}
	Instance(const UUID pid_, 
                 const string& name_,
                 const bool oper_, 
                 const bool trusted_, 
                 const bool left_,
		 const ViewId& view_id_,
		 const EVSRange range_) :
	    pid(pid_),
            operational(oper_),
            trusted(trusted_),
            left(left_),
	    view_id(view_id_),
            range(range_) 
        {
            strncpy(name, name_.c_str(), sizeof(name));
        }
	const UUID& get_pid() const {
	    return pid;
	}
	bool get_operational() const {
	    return operational;
	}
	bool get_trusted() const {
	    return trusted;
	}
        bool get_left() const
        {
            return left;
        }
	const ViewId& get_view_id() const {
	    return view_id;
	}
	const EVSRange& get_range() const {
	    return range;
	}
	
	size_t write(void* buf, 
		     const size_t buflen, const size_t offset) {
	    size_t off;
	    uint32_t b = (operational ? 0x1 : 0x0) 
                | (trusted ? 0x10 : 0x0)
                | (left ? 0x100 : 0x0);
	    if ((off = gcomm::write(b, buf, buflen, offset)) == 0) {
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
	    if ((off = gcomm::write(range.get_low(), buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = gcomm::write(range.get_high(), buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    return off;
	}
	
	size_t read(const void* buf, 
                    const size_t buflen, const size_t offset) {
	    size_t off;
	    uint32_t b;
	    if ((off = gcomm::read(buf, buflen, offset, &b)) == 0)
		return 0;
	    operational = b & 0x1;
	    trusted     = b & 0x10;
            left        = b & 0x100;
	    if ((off = pid.read(buf, buflen, off)) == 0)
		return 0;
	    if ((off = view_id.read(buf, buflen, off)) == 0)
		return 0;
	    uint32_t low, high;
	    if ((off = gcomm::read(buf, buflen, off, &low)) == 0)
		return 0;
	    if ((off = gcomm::read(buf, buflen, off, &high)) == 0)
		return 0;
	    range = EVSRange(low, high);
	    return off;
	}

	static size_t size() {
	    return 4 + UUID::size() + ViewId::size() + 4 + 4;
	}
    };
    typedef std::map<UUID, Instance> InstMap;
private:

    std::map<UUID, Instance>* instances;
protected:
    

    EVSMessage(const int version_, 
               const Type type_,
               const EVSSafetyPrefix safety_prefix_,
               const uint32_t seq_,
               const uint8_t seq_range_,
               const uint32_t aru_seq_,
               const uint8_t flags_,
               const ViewId& source_view_,
               const UUID& source_,
               const string& name_,
               const EVSGap& gap_, 
               map<UUID, Instance>* instances_) :
        version(version_),
        type(type_),
        safety_prefix(safety_prefix_),
        seq(seq_),
        seq_range(seq_range_),
        aru_seq(aru_seq_),
        flags(flags_),
        source_view(source_view_),
        source(source_),
        // name(name_),
        gap(gap_),
        instances(instances_)
    {
        strncpy(name, name_.c_str(), sizeof(name));
    }

public:        
    EVSMessage() : type(NONE), seq(SEQNO_MAX), instances(0) {
        memset(name, 0, sizeof(name));
    }
    
    EVSMessage(const EVSMessage& m) {
	*this = m;
	if (m.instances) {
	    instances = new std::map<UUID, Instance>();
	    *instances = *m.instances;
	}
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
    
    UUID get_source() const {
	return source;
    }
    
    string get_source_name() const
    {
        assert(type == JOIN || type == INSTALL);
        string ret = string(name, sizeof(name));
        // LOG_INFO("get_source_name(): " + ret);
        return ret;
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
    
    ViewId get_source_view() const {
	return source_view;
    }

    EVSGap get_gap() const {
	return gap;
    }

    const std::map<UUID, Instance>* get_instances() const {
	return instances;
    }
    
    void add_instance(const UUID& pid, 
                      const string& name,
                      const bool operational, 
		      const bool trusted, 
                      const bool left,
		      const ViewId& view_id, 
		      const EVSRange& range) {
	std::pair<std::map<UUID, Instance>::iterator, bool> i = 
	    instances->insert(make_pair(
				  pid, 
				  Instance(pid, 
                                           name,
                                           operational, 
                                           trusted, 
                                           left,
					   view_id, 
                                           range)));
	if (i.second == false)
	    throw FatalException("");
    }
    
    
    // Message serialization:

    size_t read(const void* buf, const size_t buflen, const size_t offset) {
        delete instances;
        instances = 0;
	uint8_t b;
	size_t off;
	if ((off = gcomm::read(buf, buflen, offset, &b)) == 0)
	    return 0;
	version = b & 0xf;
        if (version != 0)
        {
            LOG_TRACE("");
            return 0;
        }
	type = static_cast<Type>((b >> 4) & 0xf);
        if (type <= NONE || type > INSTALL)
        {
            LOG_TRACE("");
            return 0;
        }

	if ((off = gcomm::read(buf, buflen, off, &b)) == 0)
	    return 0;
	safety_prefix = static_cast<EVSSafetyPrefix>(b & 0xf);
	if ((off = gcomm::read(buf, buflen, off, &seq_range)) == 0)
	    return 0;
	if ((off = gcomm::read(buf, buflen, off, &flags)) == 0)
	    return 0;
	if ((off = source.read(buf, buflen, off)) == 0)
	    return 0;

	if (type == USER || type == JOIN || type == INSTALL || type == LEAVE || type == GAP) {
	    if ((off = gcomm::read(buf, buflen, off, &seq)) == 0)
		return 0;
	    if ((off = gcomm::read(buf, buflen, off, &aru_seq)) == 0)
		return 0;
	    if ((off = source_view.read(buf, buflen, off)) == 0)
		return 0;
	    if (type == JOIN || type == INSTALL) {
                if ((off = read_bytes(buf, buflen, off, name, sizeof(name))) == 0)
                {
                    return 0;
                }
		uint32_t n;
		if ((off = gcomm::read(buf, buflen, off, &n)) == 0)
		    return 0;
		instances = new std::map<UUID, Instance>();
		for (size_t i = 0; i < n; ++i) {
		    Instance inst;
		    if ((off = inst.read(buf, buflen, off)) == 0)
			return 0;
		    std::pair<std::map<UUID, Instance>::iterator, bool> ii = 		    
			instances->insert(std::pair<UUID, Instance>(inst.get_pid(), inst));
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
	if ((off = gcomm::write(b, buf, buflen, offset)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Safety prefix */
	b = safety_prefix & 0xf;	
	if ((off = gcomm::write(b, buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Seq range */
	if ((off = gcomm::write(seq_range, buf, buflen, off)) == 0) {
	    LOG_TRACE("");
	    return 0;
	}
	/* Flags */
	if ((off = gcomm::write(flags, buf, buflen, off)) == 0) {
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
	    if ((off = gcomm::write(seq, buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = gcomm::write(aru_seq, buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if ((off = source_view.write(buf, buflen, off)) == 0) {
		LOG_TRACE("");
		return 0;
	    }
	    if (type == JOIN || type == INSTALL) {
                if ((off = write_bytes(name, sizeof(name), buf, buflen, off)) == 0)
                {
                    LOG_TRACE("");
                    return 0;
                }
		if ((off = gcomm::write(UInt32(instances->size()).get(), buf, buflen, off)) == 0)
                {
		    LOG_TRACE("");
		    return 0;
		}
		for (std::map<UUID, Instance>::iterator i = instances->begin(); i != instances->end(); ++i) {
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
        case NONE:
            throw FatalException("");
	case USER:
	    return 4 + 4 + 4 + source.size() + source_view.size(); // bits + seq + aru_seq + view
	case GAP:
	    return 4 + 4 + 4 + source.size() + source_view.size() + gap.size(); // bits + seq + aru_seq + view + gap
	case DELEGATE:
	    return 4 + source.size(); // 
	case JOIN:
	case INSTALL:
	    return 4 + 4 + 4 + source.size() + source_view.size() + sizeof(name)
                + 4 + instances->size()*Instance::size();
	case LEAVE:
	    return 4 + 4 + 4 + source.size() + source_view.size();
	}
	return 0;
    }

    mutable unsigned char hdrbuf[64];
    const void* get_hdr() const {
	if (write(hdrbuf, sizeof(hdrbuf), 0) == 0)
	    throw FatalException("Short buffer");
	return hdrbuf;
    }

    size_t get_hdrlen() const {
	return size();
    }


    string to_string() const
    {
        return "evsmsg(" 
            + to_string(get_type()) + ","
            + make_int(safety_prefix).to_string() + ","
            + source.to_string() + ","
            + source_view.to_string() + ","
            + make_int(seq).to_string() + ","
            + make_int(aru_seq).to_string() + ")";
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
	LOG_DEBUG(std::string("equal() not implemented for ") + 
                  Int(a->get_type()).to_string());
    }
    return false;
}


struct EVSUserMessage : EVSMessage
{
    EVSUserMessage(const UUID& pid, 
                   const EVSSafetyPrefix sp,
                   const uint32_t seq,
                   const uint8_t seq_range,
                   const uint32_t aru_seq,
                   const ViewId& vid,
                   const uint8_t flags) :
        EVSMessage(0,
                   EVSMessage::USER,
                   sp,
                   seq,
                   seq_range,
                   aru_seq,
                   flags,
                   vid,
                   pid,
                   string(),
                   EVSGap(UUID(), EVSRange()),
                   0)
    {
    }
};

struct EVSDelegateMessage : EVSMessage
{
    EVSDelegateMessage(const UUID& pid) :
        EVSMessage(0,
                   EVSMessage::DELEGATE,
                   UNRELIABLE,
                   0,
                   0,
                   0,
                   0,
                   ViewId(),
                   pid,
                   string(),
                   EVSGap(UUID(), EVSRange()),
                   0)
    {
    }
};

struct EVSGapMessage : EVSMessage
{
    EVSGapMessage(const UUID& pid,
                  const ViewId& view_id,
                  const uint32_t seq,
                  const uint32_t aru_seq,
                  const EVSGap& gap) :
        EVSMessage(0,
                   EVSMessage::GAP,
                   UNRELIABLE,
                   seq,
                   0,
                   aru_seq,
                   0,
                   view_id,
                   pid,
                   string(),
                   gap, 
                   0)
    {
        
    }
};

struct EVSJoinMessage : EVSMessage
{
    EVSJoinMessage(const UUID& pid, 
                   const string& name,
                   const ViewId& view_id,
                   const uint32_t aru_seq, 
                   const uint32_t safe_seq) :
        EVSMessage(0,
                   EVSMessage::JOIN, 
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   name,
                   EVSGap(UUID(), EVSRange()),
                   new map<UUID, Instance>())
    {
        // LOG_INFO("JOIN message: " + name);
    }
};

struct EVSLeaveMessage : EVSMessage
{
    EVSLeaveMessage(const UUID& pid, 
                    const string& name,
                    const ViewId& view_id,
                    const uint32_t aru_seq, 
                    const uint32_t safe_seq) :
        EVSMessage(0,
                   EVSMessage::LEAVE, 
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   name,
                   EVSGap(UUID(), EVSRange()),
                   0)
    {
        
    }
};

struct EVSInstallMessage : EVSMessage
{
    EVSInstallMessage(const UUID& pid, 
                   const string& name,
                   const ViewId& view_id,
                   const uint32_t aru_seq, 
                   const uint32_t safe_seq) :
        EVSMessage(0,
                   EVSMessage::INSTALL, 
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   name,
                   EVSGap(UUID(), EVSRange()),
                   new map<UUID, Instance>())
    {
        
    }
};

END_GCOMM_NAMESPACE

#endif // EVS_MESSAGE
