//
// Copyright (C) 2010-2014 Codership Oy <info@codership.com>
//

#ifndef GALERA_UUID_HPP
#define GALERA_UUID_HPP

#include "wsrep_api.h"
#include "gu_uuid.hpp"

#include <iostream>

namespace galera
{
    inline const gu_uuid_t& to_gu_uuid(const wsrep_uuid_t& uuid)
    {
        return *reinterpret_cast<const gu_uuid_t*>(&uuid);
    }

    inline gu_uuid_t& to_gu_uuid(wsrep_uuid_t& uuid)
    {
        return *reinterpret_cast<gu_uuid_t*>(&uuid);
    }

    inline bool operator==(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
    {
        return to_gu_uuid(a) == to_gu_uuid(b);
    }

    inline bool operator!=(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
    {
        return !(a == b);
    }

    inline std::ostream& operator<<(std::ostream& os, const wsrep_uuid_t& uuid)
    {
        return os << to_gu_uuid(uuid);
    }
    inline std::istream& operator>>(std::istream& is, wsrep_uuid_t& uuid)
    {
        return is >> to_gu_uuid(uuid);
    }
    inline size_t serial_size(const wsrep_uuid_t& uuid)
    {
        return gu_uuid_serial_size(to_gu_uuid(uuid));
    }
    inline size_t serialize(const wsrep_uuid_t& uuid, gu::byte_t* buf,
                            size_t buflen, size_t offset)
    {
        return gu_uuid_serialize(to_gu_uuid(uuid), buf, buflen, offset);
    }
    inline size_t unserialize(const gu::byte_t* buf, size_t buflen,
                              size_t offset, wsrep_uuid_t& uuid)
    {
        return gu_uuid_unserialize(buf, buflen, offset, to_gu_uuid(uuid));
    }
}

#endif // GALERA_UUID_HPP
