#ifndef ADDRESS_HPP
#define ADDRESS_HPP

#include <gcomm/types.h>
#include <cassert>
#include <ostream>
#include <sstream>


class Serializable {
protected:
    Serializable() {}
public:
    virtual ~Serializable() {}
    virtual size_t read(const void *buf, size_t buflen, size_t offset) = 0;
    virtual size_t write(void *buf, size_t buflen, size_t offset) const = 0;
    virtual size_t size() const = 0;
};


class ProcId {
    uint16_t id;
public:
    ProcId() : id(0xffff) {}
    ProcId(const uint16_t i) : id(i) {}
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	return read_uint16(buf, buflen, offset, &id);
    }
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	return write_uint16(id, buf, buflen, offset);
    }
    size_t size() const {
	return sizeof(id);
    }
    bool is_invalid() const {
	return id == 0xffff;
    }
    bool is_any() const {
	return id == 0;
    }
    bool operator==(const ProcId& cmp) const {
	return id == cmp.id;
    }
    bool operator<(const ProcId& cmp) const {
	return id < cmp.id;
    }
    ProcId& operator++(int) {
	id++;
	return *this;
    }
    uint16_t to_uint() const {
	return id;
    }
};

class ServiceId {
    uint8_t id;
public:
    ServiceId() : id(0xff) {}
    ServiceId(const uint8_t i) : id(i) {}
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	return read_uint8(buf, buflen, offset, &id);
    }
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	return write_uint8(id, buf, buflen, offset);
    }
    size_t size() const {
	return sizeof(id);
    }
    bool is_invalid() const {
	return id == 0xff;
    }
    bool is_any() const {
	return id == 0;
    }
    bool operator==(const ServiceId& cmp) const {
	return id == cmp.id;
    }
    bool operator<(const ServiceId& cmp) const {
	return id < cmp.id;
    }
    bool operator!=(const ServiceId cmp) const {
	return id != cmp.id;
    }
    uint8_t to_uint() const {
	return id;
    }
};

class SegmentId {
    uint8_t id;
public:
    SegmentId() : id(0xff) {}
    SegmentId(const uint8_t i) : id(i) {}
    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	return read_uint8(buf, buflen, offset, &id);
    }
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	return write_uint8(id, buf, buflen, offset);
    }
    size_t size() const {
	return sizeof(id);
    }
    bool is_invalid() const {
	return id == 0xff;
    }
    bool is_any() const {
	return id == 0;
    }
    bool operator==(const SegmentId& cmp) const {
	return id == cmp.id;
    }
    bool operator<(const SegmentId& cmp) const {
	return id < cmp.id;
    }

    uint8_t to_uint() const {
	return id;
    }
};


class Address {
    ProcId proc_id;
    ServiceId service_id;
    SegmentId segment_id;
public:
    Address() {}
    Address(const ProcId pid, const ServiceId srid, const SegmentId sgid) 
	: proc_id(pid), service_id(srid), segment_id(sgid) {}

    size_t read(const void *buf, const size_t buflen, const size_t offset) {
	size_t off;
	if ((off = proc_id.read(buf, buflen, offset)) == 0)
	    return 0;
	if ((off = service_id.read(buf, buflen, off)) == 0)
	    return 0;
	return segment_id.read(buf, buflen, off);
	    
    }
    
    size_t write(void *buf, const size_t buflen, const size_t offset) const {
	size_t off;
	if ((off = proc_id.write(buf, buflen, offset)) == 0)
	    return 0;
	if ((off = service_id.write(buf, buflen, off)) == 0)
	    return 0;
	return segment_id.write(buf, buflen, off);

    }
    size_t size() const {
	return proc_id.size() + service_id.size() + segment_id.size();
    }




    bool is_invalid() const {
	return proc_id.is_invalid() || service_id.is_invalid() || segment_id.is_invalid();
    }
    bool is_any() const {
	return proc_id.is_any() && service_id.is_any() && segment_id.is_any();
    }
    bool is_any_proc() const {
	return proc_id.is_any();
    }
    bool is_any_service() const {
	return service_id.is_any();
    }
    bool is_any_segment() const {
	return segment_id.is_any();
    }    

    bool is_same_proc(const Address& cmp) const {
	return cmp.proc_id == proc_id;
    }

    bool is_same_service(const Address& cmp) const {
	return cmp.service_id == service_id;
    }

    bool is_same_segment(const Address& cmp) const {
	return cmp.segment_id == segment_id;
    }

    bool operator==(const Address& cmp) const {
	return proc_id == cmp.proc_id && 
	    service_id == cmp.service_id &&
	    segment_id == cmp.segment_id;
    }

    bool operator<(const Address& cmp) const {
	if (segment_id < cmp.segment_id)
	    return true;
	if (service_id < cmp.service_id)
	    return true;
	if (proc_id < cmp.proc_id)
	    return true;
	return false;
    }

    ProcId get_proc_id() const {
	return proc_id;
    }
    ServiceId get_service_id() const {
	return service_id;
    }
    SegmentId get_segment_id() const {
	return segment_id;
    }

    uint32_t to_uint() const {
	uint32_t val = segment_id.to_uint();
	val <<= 8;
	val |= service_id.to_uint();
	val <<= 16;
	val |= proc_id.to_uint();
	return val;
    }

    std::string to_string() const {
	std::ostringstream os;
	os << "(" << static_cast<unsigned int>(get_proc_id().to_uint()) << ",";
	os << static_cast<unsigned int>(get_service_id().to_uint()) << ",";
	os << static_cast<unsigned int>(get_segment_id().to_uint()) << ")";
	return os.str();
    }

};

static const Address ADDRESS_INVALID;

inline std::ostream& operator<<(std::ostream& os, const Address a)
{
    return os << a.to_string();
}



// Conventional shorthand for address set
#include <set>
typedef std::set<Address> Aset;

#endif /* ADDRESS_HPP */
