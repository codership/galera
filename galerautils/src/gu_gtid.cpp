/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#include "gu_gtid.hpp"
#include "gu_byteswap.hpp"

#include <cassert>

void
gu::GTID::print(std::ostream& os) const
{
    os << uuid_ << ':' << seqno_;
}

void
gu::GTID::scan(std::istream& is)
{
    UUID u;
    char c;
    seqno_t s;

    try
    {
        is >> u >> c >> s;
    }
    catch (std::exception& e)
    {
        gu_throw_error(EINVAL) << e.what();
    }

    if (c != ':')
    {
        gu_throw_error(EINVAL) << "Malformed GTID: '" << u << c << s << '\'';
    }

    uuid_  = u;
    seqno_ = s;
}

size_t
gu::GTID::serialize(void* const buf, size_t const buflen, size_t const offset)
    const
{
    if (gu_unlikely(buflen - offset < serial_size()))
    {
        gu_throw_error(EMSGSIZE) << "Buffer too short for GTID: "
                                 << buflen - offset;
    }

    byte_t* const b(static_cast<byte_t*>(buf) + offset);
    seqno_t const s(htog(seqno_));

    assert(serial_size() == (sizeof(uuid_) + sizeof(s)));

    ::memcpy(b, &uuid_, sizeof(uuid_));
    ::memcpy(b + sizeof(uuid_), &s, sizeof(s));

    return offset + serial_size();
}

size_t
gu::GTID::unserialize(const void* buf, size_t buflen, size_t offset)
{
    if (gu_unlikely(buflen - offset < serial_size()))
    {
        gu_throw_error(EMSGSIZE) << "Buffer too short for GTID: "
                                 << buflen - offset;
    }

    const byte_t* const b(static_cast<const byte_t*>(buf) + offset);
    seqno_t s;

    assert(serial_size() == (sizeof(uuid_) + sizeof(s)));

    ::memcpy(&uuid_, b, sizeof(uuid_));
    ::memcpy(&s, b + sizeof(uuid_), sizeof(s));

    seqno_ = gtoh(s);

    return offset + serial_size();
}
