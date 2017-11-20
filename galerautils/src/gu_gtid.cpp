/*
 * Copyright (C) 2015-2017 Codership Oy <info@codership.com>
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
