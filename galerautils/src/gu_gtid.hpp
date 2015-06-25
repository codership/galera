/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#ifndef _gu_gtid_hpp_
#define _gu_gtid_hpp_

#include "gu_uuid.hpp"
#include "gu_byteswap.hpp"

#include <stdint.h>

namespace gu
{
    class GTID;

    typedef int64_t seqno_t;
} /* namespace gu */

class gu::GTID
{
public:

    static seqno_t const SEQNO_UNDEFINED = -1;

    GTID() : uuid_(), seqno_(SEQNO_UNDEFINED) {}

    GTID(const UUID& u, seqno_t s) : uuid_(u), seqno_(s) {}

    GTID(const gu_uuid_t& u, seqno_t s) : uuid_(u), seqno_(s) {}

    explicit GTID(const GTID& g) : uuid_(g.uuid_), seqno_(g.seqno_) {}

    GTID(const void* const buf, size_t const buflen)
        :
        uuid_ (),
        seqno_(SEQNO_UNDEFINED)
    {
        (void) unserialize(buf, buflen, 0);
    }

    // this constuftor modifies offset
    GTID(const void* const buf, size_t const buflen, size_t& offset)
        :
        uuid_ (),
        seqno_(SEQNO_UNDEFINED)
    {
        offset = unserialize(buf, buflen, offset);
    }

    const UUID& uuid()  const { return uuid_;  }
    seqno_t     seqno() const { return seqno_; }

    void set(const gu::UUID& u) { uuid_  = u; }
    void set(seqno_t const   s) { seqno_ = s; }

    bool operator==(const GTID& other) const
    {
        return (seqno_ == other.seqno_ && uuid_ == other.uuid_);
    }

    bool operator!=(const GTID& other) const { return !(*this == other); }

    bool is_undefined() const
    {
        static GTID undefined;
        return *this == undefined;
    }

    void print(std::ostream& os) const;

    void scan(std::istream& is);

    static size_t serial_size() { return UUID::serial_size() + sizeof(seqno_); }

    size_t serialize_unchecked(void* const buf, size_t const buflen,
                               size_t offset) const
    {
        assert(serial_size() == (uuid_.serial_size() + sizeof(seqno_)));
        assert(buflen - offset >= serial_size());

        offset = uuid_.serialize_unchecked(buf, buflen, offset);

        void* const seqno_ptr(static_cast<byte_t*>(buf) + offset);
        *static_cast<seqno_t*>(seqno_ptr) = htog(seqno_);

        return offset + sizeof(seqno_t);
    }

    size_t unserialize_unchecked(const void* const buf, size_t const buflen,
                                 size_t offset)
    {
        assert(serial_size() == (uuid_.serial_size() + sizeof(seqno_)));
        assert(buflen - offset >= serial_size());

        offset = uuid_.unserialize_unchecked(buf, buflen, offset);

        const void* const seqno_ptr(static_cast<const byte_t*>(buf) + offset);
        seqno_ = gtoh(*static_cast<const seqno_t*>(seqno_ptr));

        return offset + sizeof(seqno_t);
    }

    size_t serialize  (void* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const void* buf, size_t buflen, size_t offset);

private:

    UUID    uuid_;
    seqno_t seqno_;

}; /* class GTID */

namespace gu
{
inline std::ostream& operator<< (std::ostream& os, const GTID& gtid)
{
    gtid.print(os); return os;
}

inline std::istream& operator>> (std::istream& is, GTID& gtid)
{
    gtid.scan(is); return is;
}
} /* namespace gu */

#endif /* _gu_gtid_hpp_ */
