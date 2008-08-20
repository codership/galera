#ifndef EVS_MESSAGE_H
#define EVS_MESSAGE_H

#include <cstdlib>

#include "gcomm/exception.hpp"

class EVSViewId {
    uint8_t uuid[8];
public:
    EVSViewId() {
	memset(uuid, 0, 8);
    }
    EVSViewId(const Sockaddr& sa, const uint32_t seq) {
	write_uint32(::rand(), uuid, sizeof(uuid), 0);
	write_uint32(seq, uuid, sizeof(uuid), 4);
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

    size_t size() const {
	return 8;
    }

    bool operator<(const EVSViewId& cmp) const {
	return memcmp(uuid, cmp.uuid, 8) < 0;
    }
    bool operator==(const EVSViewId& cmp) const {
	return memcmp(uuid, cmp.uuid, 8) == 0;
    }
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

    enum SafetyPrefix {
	DROP,
	UNRELIABLE,
	FIFO,
	AGREED,
	SAFE
    };

    enum Flag {
	F_MSG_MORE = 0x1
    };

private:
    int version;
    Type type;
    SafetyPrefix safety_prefix;
    uint32_t seq;
    uint8_t seq_range;
    uint8_t flags;
    EVSViewId source_view;
public:    


    EVSMessage() : seq(SEQNO_MAX) {}
    
    // User message
    EVSMessage(const Type type_, 
	       const SafetyPrefix sp_, 
	       const uint32_t seq_, 
	       const uint8_t seq_range_,
	       const EVSViewId vid_, const uint8_t flags_) : 
	version(0), 
	type(type_), 
	safety_prefix(sp_), 
	seq(seq_), 
	seq_range(seq_range_),
	flags(flags_),
	source_view(vid_) {
	if (type != USER)
	    throw FatalException("Invalid type");
    }
    

    Type get_type() const {
	return type;
    }
    
    SafetyPrefix get_safety_prefix() const {
	return safety_prefix;
    }
    
    uint32_t get_seq() const {
	return seq;
    }
    
    uint8_t get_seq_range() const {
	return seq_range;
    }

    uint8_t get_flags() const {
	return flags;
    }
    
    EVSViewId get_source_view() const {
	return source_view;
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
	safety_prefix = static_cast<SafetyPrefix>(b & 0xf);
	if ((off = read_uint8(buf, buflen, off, &seq_range)) == 0)
	    return 0;
	if ((off = read_uint8(buf, buflen, off, &flags)) == 0)
	    return 0;
	if ((off = read_uint32(buf, buflen, off, &seq)) == 0)
	    return 0;
	if ((off = source_view.read(buf, buflen, off)) == 0)
	    return 0;
	return off;
    }
    
    size_t write(void* buf, const size_t buflen, const size_t offset) const {
	uint8_t b;
	size_t off;
	
	b = (version & 0xf) | ((type << 4) & 0xf0);
	if ((off = write_uint8(b, buf, buflen, offset)) == 0)
	    return 0;
	b = safety_prefix & 0xf;
	
	if ((off = write_uint8(b, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_uint8(seq_range, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_uint8(flags, buf, buflen, off)) == 0)
	    return 0;
	if ((off = write_uint32(seq, buf, buflen, off)) == 0)
	    return 0;
	if ((off = source_view.write(buf, buflen, off)) == 0)
	    return 0;
	return off;
    }
    
    size_t size() const {
	if (type == EVSMessage::USER)
	    return 4 + 4 + source_view.size(); // bits + seq + view
	return 0;
    }
    
};

#endif // EVS_MESSAGE
