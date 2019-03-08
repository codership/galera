/*
 * Copyright (C) 2015-2017 Codership Oy <info@codership.com>
 */

#ifndef _gu_gtid_hpp_
#define _gu_gtid_hpp_

#include "gu_uuid.hpp"
#include "gu_serialize.hpp"

#include "gu_hash.h"
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

    GTID(const GTID& g) : uuid_(g.uuid_), seqno_(g.seqno_) {}

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

    GTID& operator=(const GTID& other) = default;

    const UUID& uuid()  const { return uuid_;  }
    seqno_t     seqno() const { return seqno_; }

    void set(const gu::UUID& u) { uuid_  = u; }
    void set(seqno_t const   s) { seqno_ = s; }

    void set(const gu::UUID& u, seqno_t const s) { set(u); set(s); }

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

    static size_t serial_size() { return UUID::serial_size() +sizeof(int64_t); }

    size_t serialize(void* const buf, size_t offset) const
    {
        assert(serial_size() == (uuid_.serial_size() + sizeof(int64_t)));

        offset = uuid_.serialize(buf, offset);
        offset = gu::serialize8(seqno_, buf, offset);

        return offset;
    }

    size_t unserialize(const void* const buf, size_t offset)
    {
        assert(serial_size() == (uuid_.serial_size() + sizeof(seqno_)));

        offset = uuid_.unserialize(buf, offset);
        offset = gu::unserialize8(buf, offset, seqno_);

        return offset;
    }

    size_t unserialize(const void* const buf, const size_t buflen,
                       const size_t offset)
    {
        gu_trace(gu::check_bounds(offset + serial_size(), buflen));
        return unserialize(buf, offset);
    }

    size_t serialize  (void* const buf, const size_t buflen,
                       const size_t offset) const
    {
        gu_trace(gu::check_bounds(offset + serial_size(), buflen));
        return serialize(buf, offset);
    }

    class TableHash // for std::map, does not have to be endian independent
    {
    public:
        size_t operator()(const GTID& gtid) const
        {
            // UUID is 16 bytes and seqno_t is 8 bytes so all should be
            // properly aligned into a continuous buffer
            return gu_table_hash(&gtid, sizeof(UUID) + sizeof(seqno_t));
        }
    };

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
