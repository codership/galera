/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#include "gu_gtid.hpp"
#include "gu_throw.hpp"

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

    return serialize_unchecked(buf, buflen, offset);
}

size_t
gu::GTID::unserialize(const void* const buf, size_t const buflen,
                      size_t const offset)
{
    if (gu_unlikely(buflen - offset < serial_size()))
    {
        gu_throw_error(EMSGSIZE) << "Buffer too short for GTID: "
                                 << buflen - offset;
    }

    return unserialize_unchecked(buf, buflen, offset);
}
