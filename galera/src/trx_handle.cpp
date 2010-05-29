//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#include "trx_handle.hpp"

namespace galera {

std::ostream&
operator<<(std::ostream& os, const TrxHandle& th)
{
    os << "(l: "  << th.get_local_seqno()
       << ", g: " << th.get_global_seqno()
       << ", s: " << th.get_last_seen_seqno()
       << ", d: " << th.get_last_depends_seqno()
       << ')';

    return os;
}

}

