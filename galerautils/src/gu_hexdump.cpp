// Copyright (C) 2013 Codership Oy <info@codership.com>
/**
 * @file operator << for hexdump - definiton
 *
 * $Id$
 */

#include "gu_hexdump.hpp"

#include "gu_hexdump.h"
#include "gu_logger.hpp"

namespace gu {

static size_t const hexdump_bytes_per_go(GU_HEXDUMP_BYTES_PER_LINE * 2);
static size_t const hexdump_reserve_string(
    hexdump_bytes_per_go*2 /* chars */ + hexdump_bytes_per_go/4 /* whitespace */
    + 1 /* \0 */
);

std::ostream&
Hexdump::to_stream (std::ostream& os) const
{
    char   str[hexdump_reserve_string];
    size_t off(0);

    while (off < size_)
    {
        size_t const to_print(std::min(size_ - off, hexdump_bytes_per_go));

        gu_hexdump (buf_ + off, to_print, str, sizeof(str), alpha_);

        off += to_print;

        os << str; if (off < size_) os << '\n';
    }

    return os;
}

} // namespace gu


