#ifndef EVS_MESSAGE_H
#define EVS_MESSAGE_H



#include "gcomm/common.hpp"
#include "gcomm/exception.hpp"
#include "gcomm/logger.hpp"
#include "gcomm/time.hpp"
#include "gcomm/util.hpp"
#include "gcomm/uuid.hpp"
#include "gcomm/view.hpp"
#include "gcomm/map.hpp"
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
        return std::string("[") + make_int(low).to_string() 
            + "," + make_int(high).to_string() + "]";
    }
};

struct EVSGap {
    UUID source;
    EVSRange range;
    EVSGap() :
        source(),
        range()
    {
    }

    EVSGap(const UUID& source_, const EVSRange& range_) :
	source(source_), 
        range(range_) 
    {
    }

    UUID get_source() const 
    {
	return source;
    }
    uint32_t get_low() const 
    {
	return range.low;
    }

    uint32_t get_high() const 
    {
	return range.high;
    }

    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        size_t off;

        gu_trace (off = source.read(buf, buflen, offset));
        gu_trace (off = gcomm::read(buf, buflen, off, &range.low));
        gu_trace (off = gcomm::read(buf, buflen, off, &range.high));

        return off;
    }

    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        size_t off;

        gu_trace (off = source.write(buf, buflen, offset));
        gu_trace (off = gcomm::write(range.low, buf, buflen, off));
        gu_trace (off = gcomm::write(range.high, buf, buflen, off));

        return off;
    }

    size_t size() const 
    {
        return source.size() + 8;
    }

    std::string to_string() const
    {
        return source.to_string() + ":" + range.to_string();
    }
};

inline bool operator==(const EVSGap& a, const EVSGap& b)
{
    return a.range == b.range && a.source == b.source;
}

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


    static std::string to_string(const Type t) 
    {
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
        F_RESEND =   0x2,
        F_SOURCE =   0x4
    };

private:
    int version;
    Type type;
    uint8_t user_type;
    EVSSafetyPrefix safety_prefix;
    uint32_t seq;
    uint8_t seq_range;
    uint32_t aru_seq;
    uint8_t flags;
    ViewId source_view;
    UUID source;
    EVSGap gap;
    int64_t fifo_seq;
    mutable Time tstamp;
public:
    class Instance {
	UUID pid;
	bool operational;
        bool left;
	ViewId view_id;
	EVSRange range;
        uint32_t safe_seq;
    public:
        Instance() :
            pid(),
            operational(),
            left(),
            view_id(),
            range(),
            safe_seq()
        {
        }
	Instance(const UUID pid_, 
                 const bool oper_, 
                 const bool left_,
		 const ViewId& view_id_,
		 const EVSRange range_,
                 const uint32_t safe_seq_) :
	    pid(pid_),
            operational(oper_),
            left(left_),
	    view_id(view_id_),
            range(range_),
            safe_seq(safe_seq_)
        {
        }
	const UUID& get_pid() const {
	    return pid;
	}
        const UUID& get_uuid() const
        {
            return pid;
        }
	bool get_operational() const {
	    return operational;
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

        uint32_t get_safe_seq() const
        {
            return safe_seq;
        }

	size_t write(byte_t* buf, const size_t buflen, const size_t offset)
            const throw (gu::Exception)
        {
	    size_t   off;
	    uint32_t b = (operational ? 0x1 : 0x0) | (left ? 0x10 : 0x0);

	    gu_trace (off = gcomm::write(b, buf, buflen, offset));
	    gu_trace (off = pid.write(buf, buflen, off));
	    gu_trace (off = view_id.write(buf, buflen, off));
	    gu_trace (off = gcomm::write(range.get_low(), buf, buflen, off));
	    gu_trace (off = gcomm::write(range.get_high(), buf, buflen, off));
            gu_trace (off = gcomm::write(safe_seq, buf, buflen, off));

	    return off;
	}
	
	size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
            throw (gu::Exception)
        {
	    size_t   off;
	    uint32_t b;

	    gu_trace (off = gcomm::read(buf, buflen, offset, &b));

	    operational = b & 0x1;
            left        = b & 0x10;

	    gu_trace (off = pid.read(buf, buflen, off));
	    gu_trace (off = view_id.read(buf, buflen, off));

	    uint32_t low, high;

	    gu_trace (off = gcomm::read(buf, buflen, off, &low));
	    gu_trace (off = gcomm::read(buf, buflen, off, &high));

	    range = EVSRange(low, high);

            gu_trace (off = gcomm::read(buf, buflen, off, &safe_seq));

	    return off;
	}

	static size_t size() {
	    return 4 + UUID::size() + ViewId::size() + 4 + 4 + 4;
	}

        std::string to_string() const
        {
            std::ostringstream ret;

            ret << "inst(" << pid.to_string() << ") "
                << (operational ? "o=1" : "o=0") << ", "
                << (left ? "l=1" : "l=0") << ", "
                << view_id.to_string() << ", "
                << range.to_string() << " safe_seq: " << safe_seq;

            return ret.str();
        }
    };

    class InstMap : public Map<UUID, Instance, std::map<const UUID, Instance> > { };
private:

    InstMap* instances;

protected:

    EVSMessage(const int             version_, 
               const Type            type_,
               const uint8_t         user_type_,
               const EVSSafetyPrefix safety_prefix_,
               const uint32_t        seq_,
               const uint8_t         seq_range_,
               const uint32_t        aru_seq_,
               const uint8_t         flags_,
               const ViewId&         source_view_,
               const UUID&           source_,
               const EVSGap&         gap_,
               const int64_t         fifo_seq_,
               InstMap*              instances_)
        :
        version       (version_),
        type          (type_),
        user_type     (user_type_),
        safety_prefix (safety_prefix_),
        seq           (seq_),
        seq_range     (seq_range_),
        aru_seq       (aru_seq_),
        flags         (flags_),
        source_view   (source_view_),
        source        (source_),
        gap           (gap_),
        fifo_seq      (fifo_seq_),
        tstamp        (Time::now()),
        instances     (instances_)
    {
        if (source != UUID::nil()) flags |= F_SOURCE;
    }

public:

    EVSMessage() :
        version(),
        type(NONE), 
        user_type(),
        safety_prefix(),
        seq(SEQNO_MAX), 
        seq_range(),
        aru_seq(),
        flags(0),
        source_view(),
        source(),
        gap(),
        fifo_seq(-1),
        tstamp(Time::now()),
        instances(0) 
    {
    }
    
    EVSMessage(const EVSMessage& m) :
        version(m.version),
        type(m.type),
        user_type(m.user_type),
        safety_prefix(m.safety_prefix),
        seq(m.seq),
        seq_range(m.seq_range),
        aru_seq(m.aru_seq),
        flags(m.flags),
        source_view(m.source_view),
        source(m.source),
        gap(m.gap),
        fifo_seq(m.fifo_seq),
        tstamp(m.tstamp),
        instances(0)
    {
	if (m.instances) 
        {
	    instances = new InstMap(*m.instances);
	}
    }
    
    virtual ~EVSMessage() 
    {
	delete instances;
    }


    EVSMessage& operator=(const EVSMessage& m)
    {
        version = m.version;
        type = m.type;
        user_type = m.user_type;
        safety_prefix = m.safety_prefix;
        seq = m.seq;
        seq_range = m.seq_range;
        aru_seq = m.aru_seq;
        flags = m.flags;
        source_view = m.source_view;
        source = m.source;
        gap = m.gap;
        fifo_seq = m.fifo_seq;
        tstamp = m.tstamp;
        instances = m.instances != 0 ? new InstMap(*m.instances) : 0;
        return *this;
    }
    
    Type get_type() const 
    {
	return type;
    }
    
    bool is_membership() const
    {
        return type == JOIN || type == INSTALL || type == LEAVE;
    }

    uint8_t get_user_type() const
    {
        return user_type;
    }

    EVSSafetyPrefix get_safety_prefix() const {
	return safety_prefix;
    }
    
    const UUID& get_source() const {
	return source;
    }
    
    void set_source(const UUID& uuid)
    {
        source = uuid;
    }
    
    
    uint32_t get_seq() const {
	return seq;
    }
    
    uint8_t get_seq_range() const {
	return seq_range;
    }

    void set_aru_seq(const uint32_t aru_seq)
    {
        this->aru_seq = aru_seq;
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

    int64_t get_fifo_seq() const
    {
        return fifo_seq;
    }

    void set_tstamp(const Time& t) const
    {
        tstamp = t;
    }
    
    const Time& get_tstamp() const
    {
        return tstamp;
    }
    
    const InstMap* get_instances() const { return instances; }
    
    void add_instance(const UUID& pid, 
                      const bool operational, 
                      const bool left,
		      const ViewId& view_id, 
		      const EVSRange& range,
                      const uint32_t safe_seq) 
    {
        (void)instances->insert_checked(make_pair(
                                            pid, 
                                            Instance(pid, 
                                                     operational, 
                                                     left,
                                                     view_id, 
                                                     range, safe_seq)));
    }
    
    
    // Message serialization:

    size_t read(const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        delete instances;

        instances = 0;

	uint8_t b;
	size_t  off;

	gu_trace (off = gcomm::read(buf, buflen, offset, &b));

	version       = b & 0x3;
	type          = static_cast<Type>((b >> 2) & 0x7);
	safety_prefix = static_cast<EVSSafetyPrefix>((b >> 5) & 0x7);

        if (version != 0)
        {
            gcomm_throw_runtime (EPROTONOSUPPORT)
                << "Unsupported protocol version: " << version;
        }

        if (type <= NONE || type > INSTALL)
        {
            gcomm_throw_runtime (EINVAL) << "Wrong EVSMessage type: " << type;
        }
        
        if (safety_prefix < DROP || safety_prefix > SAFE)
        {
            gcomm_throw_runtime (EINVAL)
                << "Bad safety prefix: " << safety_prefix;
        }
        
	gu_trace (off = gcomm::read(buf, buflen, off, &user_type));
	gu_trace (off = gcomm::read(buf, buflen, off, &seq_range));
	gu_trace (off = gcomm::read(buf, buflen, off, &flags));
        
        if (flags & F_SOURCE)
        {
            gu_trace (off = source.read(buf, buflen, off));
        }

	if (type == USER || type == JOIN || type == INSTALL || type == LEAVE ||
            type == GAP)
        {
	    gu_trace (off = gcomm::read(buf, buflen, off, &seq));
	    gu_trace (off = gcomm::read(buf, buflen, off, &aru_seq));
	    gu_trace (off = source_view.read(buf, buflen, off));

            if (type == JOIN || type == INSTALL || type == LEAVE)
            {
                gu_trace (off = gcomm::read(buf, buflen, off, &fifo_seq));
            }
            else
            {
                fifo_seq = -1;
            }
            
	    if (type == JOIN || type == INSTALL)
            {
		instances = new InstMap();
                gu_trace(off = instances->unserialize(buf, buflen, off));
	    }
            else if (type == GAP)
            {
                gu_trace (off = gap.read(buf, buflen, off));
            }
	}

	return off;
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
	int b;
	size_t off;
	
	/* Common header for all messages */
	/* Version, type, safety_prefix */
        b = safety_prefix & 0x7;
        b <<= 3;
        b |= (type & 0x7);
        b <<= 2;
	b |= (version & 0x3);

	gu_trace (off = gcomm::write(static_cast<uint8_t>(b), buf, buflen, offset));

        /* User type */
	gu_trace (off = gcomm::write(user_type, buf, buflen, off));

	/* Seq range */
	gu_trace (off = gcomm::write(seq_range, buf, buflen, off));

	/* Flags */
	gu_trace (off = gcomm::write(flags, buf, buflen, off));

        if (flags & F_SOURCE)
        {
            /* Message source pid */
            gu_trace (off = source.write(buf, buflen, off));
        }
	
	if (type == USER || type == JOIN || type == INSTALL || type == LEAVE ||
	    type == GAP) 
        {
	    gu_trace (off = gcomm::write(seq, buf, buflen, off));
	    gu_trace (off = gcomm::write(aru_seq, buf, buflen, off));
	    gu_trace (off = source_view.write(buf, buflen, off));

            if (type == JOIN || type == INSTALL || type == LEAVE)
            {
                gu_trace (off = gcomm::write(fifo_seq, buf, buflen, off));
            }
            
	    if (type == JOIN || type == INSTALL)
            {
                gu_trace (off = instances->serialize(buf, buflen, off));
	    }
            else if (type == GAP)
            {
                gu_trace (off = gap.write(buf, buflen, off));
            }
	} 
	return off;
    }
    
    size_t size() const
    {
        size_t source_size = flags & F_SOURCE ? source.size() : 0;
        
	switch (type) {
        case NONE:
            gcomm_throw_fatal << "Invalid message type NONE";
	case USER:
            //                 bits seq aru_seq        view
	    return source_size + 4 + 4  +  4  +  source_view.size();
	case GAP:
            //                 bits seq aru_seq     view               gap
	    return source_size + 4 + 4  +  4  + source_view.size() + gap.size();
	case DELEGATE:
	    return source.size() + 4; // @todo:???
	case JOIN:
	case INSTALL:
	    return source_size + 4 + 4 + 4 + 8 + source_view.size()
                + 4 + instances->serial_size();
	case LEAVE:
	    return source_size + 4 + 4 + 4 + 8 + source_view.size();
	}

	return 0;
    }
    
    mutable unsigned char hdrbuf[64];

    const byte_t* get_hdr() const
    {
	if (write(hdrbuf, sizeof(hdrbuf), 0) == 0)
	    gcomm_throw_fatal << "Short buffer";

	return hdrbuf;
    }

    size_t get_hdrlen() const {
	return size();
    }


    std::string to_string() const
    {
        std::ostringstream ret;

        ret << "evsmsg("
            << "type: "     << to_string(get_type())   << ", "
            << "safetyp: "  << safety_prefix           << ", "
            << "src: ("     << source.to_string()      << "), "
            << "srcview: "  << source_view.to_string() << ", "
            << "seq: "      << seq                     << ", "
            << "aru_seq: "  << aru_seq                 << ", "
            << "fifo_seq: " << fifo_seq                << ", "
            << "gap: "      << gap.to_string()         << ", ";

        if (instances)
        {
            ret << "\ninstances: ";
            for (InstMap::const_iterator i = instances->begin();
                 i != instances->end(); ++i)
            {
                ret << i->first.to_string() << ":" << i->second.to_string()
                    << " ";
            }
        }

        ret << ")";

        return ret.str();
    }
};

// Compare two evs messages
inline bool equal(const EVSMessage* a, const EVSMessage* b)
{
    if (a->get_type() != b->get_type()) return false;

    switch (a->get_type()) {
    case EVSMessage::JOIN:
    default:
	log_debug << "equal() not implemented for " << a->get_type();
    }

    return false;
}


struct EVSUserMessage : EVSMessage
{
    EVSUserMessage(const UUID& pid, 
                   const uint8_t user_type,
                   const EVSSafetyPrefix sp,
                   const uint32_t seq,
                   const uint8_t seq_range,
                   const uint32_t aru_seq,
                   const ViewId& vid,
                   const uint8_t flags) :
        EVSMessage(0,
                   EVSMessage::USER,
                   user_type,
                   sp,
                   seq,
                   seq_range,
                   aru_seq,
                   flags,
                   vid,
                   pid,
                   EVSGap(UUID(), EVSRange()),
                   -1,
                   0)
    {
    }
};

struct EVSDelegateMessage : EVSMessage
{
    EVSDelegateMessage(const UUID& pid) :
        EVSMessage(0,
                   EVSMessage::DELEGATE,
                   0xff,
                   UNRELIABLE,
                   0,
                   0,
                   0,
                   0,
                   ViewId(),
                   pid,
                   EVSGap(UUID(), EVSRange()),
                   -1,
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
                   0xff,
                   UNRELIABLE,
                   seq,
                   0,
                   aru_seq,
                   0,
                   view_id,
                   pid,
                   gap, 
                   -1,
                   0)
    {
        
    }
};

struct EVSJoinMessage : EVSMessage
{
    EVSJoinMessage(const UUID& pid, 
                   const ViewId& view_id,
                   const uint32_t aru_seq, 
                   const uint32_t safe_seq,
                   const int64_t fifo_seq) :
        EVSMessage(0,
                   EVSMessage::JOIN, 
                   0xff,
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   EVSGap(UUID(), EVSRange()),
                   fifo_seq,
                   new InstMap())
    {
    }
};

struct EVSLeaveMessage : EVSMessage
{
    EVSLeaveMessage(const UUID& pid, 
                    const ViewId& view_id,
                    const uint32_t aru_seq, 
                    const uint32_t safe_seq,
                    const int64_t fifo_seq) :
        EVSMessage(0,
                   EVSMessage::LEAVE, 
                   0xff,
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   EVSGap(UUID(), EVSRange()),
                   fifo_seq,
                   0)
    {
        
    }
};

struct EVSInstallMessage : EVSMessage
{
    EVSInstallMessage(const UUID& pid, 
                      const ViewId& view_id,
                      const uint32_t aru_seq, 
                      const uint32_t safe_seq,
                      const int64_t fifo_seq) :
        EVSMessage(0,
                   EVSMessage::INSTALL, 
                   0xff,
                   UNRELIABLE,
                   safe_seq,
                   uint8_t(0),
                   aru_seq,
                   uint8_t(0),
                   view_id, 
                   pid,
                   EVSGap(UUID(), EVSRange()),
                   fifo_seq,
                   new InstMap())
    {
        
    }
};

END_GCOMM_NAMESPACE

#endif // EVS_MESSAGE
