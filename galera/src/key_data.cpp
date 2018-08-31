//
// Copyright (C) 2018 Codership Oy <info@codership.com>
//

#include "key_data.hpp"

#include <gu_hexdump.hpp>

void
galera::KeyData::print(std::ostream& os) const
{
    os << "proto: " << proto_ver << ", type: " << type << ", copy: "
       << (copy ? "yes" : "no") << ", parts(" << parts_num << "):";

    for (int i = 0; i < parts_num; ++i)
    {
        os << "\n\t" << gu::Hexdump(parts[i].ptr, parts[i].len, true);
    }
}
