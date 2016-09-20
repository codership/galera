/*
 * Copyright (C) 2015 Codership Oy <info@codership.com>
 */

#include "gcs_code_msg.hpp"

void
gcs::core::CodeMsg::print(std::ostream& os) const
{
    os << gu::GTID(uuid(), seqno()) << ',' << code();
}
