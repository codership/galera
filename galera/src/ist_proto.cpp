//
// Copyright (C) 2015 Codership Oy <info@codership.com>
//

#include "ist_proto.hpp"

std::ostream&
galera::ist::operator<< (std::ostream& os, const Message& m)
{
    os << "ver: "     << m.version()
       << ", type: "  << m.type()
       << ", flags: " << m.flags()
       << ", ctrl: "  << m.ctrl()
       << ", len: "   << m.len()
       << ", seqno: " << m.seqno();

    return os;
}

void
galera::ist::Message::throw_invalid_version(uint8_t const v)
{
    gu_throw_error(EPROTO) << "invalid protocol version " << int(v)
                           << ", expected " << version_;
}

void
galera::ist::Message::throw_corrupted_header()
{
    gu_throw_error(EINVAL) << "Corrupted IST message header: " << *this;
}
