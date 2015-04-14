/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#ifndef _gu_gtid_hpp_
#define _gu_gtid_hpp_

#include "gu_uuid.hpp"

#include <stdint.h>

#include <ostream>
#include <istream>

namespace gu
{
    class GTID;

    typedef int64_t seqno_t;
}

class gu::GTID
{
public:

    GTID() : uuid_(), seqno_(-1) {}

    GTID(const UUID& u, seqno_t s) : uuid_(u), seqno_(s) {}

    GTID(const gu_uuid_t& u, seqno_t s) : uuid_(u), seqno_(s) {}

    GTID(const GTID& g) : uuid_(g.uuid_), seqno_(g.seqno_) {}

    GTID(const gu::byte_t* const buf, size_t const buflen)
        :
        uuid_ (),
        seqno_(-1)
    {
        (void) unserialize(buf, buflen, 0);
    }

    GTID(const gu::byte_t* const buf, size_t const buflen, size_t& offset)
        :
        uuid_ (),
        seqno_(-1)
    {
        offset = unserialize(buf, buflen, offset);
    }

    const UUID& uuid() const { return uuid_; }

    seqno_t seqno() const { return seqno_; }

    bool operator==(const GTID& other) const
    {
        return (seqno_ == other.seqno_ && uuid_ == other.uuid_);
    }

    bool operator!=(const GTID& other) const { return !(*this == other); }

    void print(std::ostream& os) const;

    void scan(std::istream& is);

    static size_t serial_size() { return sizeof(uuid_) + sizeof(seqno_); }

    size_t serialize  (void* buf, size_t buflen, size_t offset) const;
    size_t unserialize(const void* buf, size_t buflen, size_t offset);

private:

    UUID    uuid_;
    seqno_t seqno_;

}; /* class GTID */

inline std::ostream& operator<< (std::ostream& os, const gu::GTID& gtid)
{
    gtid.print(os); return os;
}

inline std::istream& operator>> (std::istream& is, gu::GTID& gtid)
{
    gtid.scan(is); return is;
}

#endif /* _gu_gtid_hpp_ */
