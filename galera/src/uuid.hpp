//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_UUID_HPP
#define GALERA_UUID_HPP

#include "wsrep_api.h"
#include "gu_uuid.h"
#include "gu_assert.hpp"

#include <iostream>

inline bool operator==(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
{
    return (memcmp(&a, &b, sizeof(a)) == 0);
}

inline bool operator!=(const wsrep_uuid_t& a, const wsrep_uuid_t& b)
{
    return !(a == b);
}


namespace galera
{
    inline std::ostream& operator<<(std::ostream& os, const wsrep_uuid_t& uuid)
    {
        char uuid_buf[GU_UUID_STR_LEN + 1];
        ssize_t ret(gu_uuid_print(reinterpret_cast<const gu_uuid_t*>(&uuid),
                                  uuid_buf, sizeof(uuid_buf)));
        (void)ret;

        assert(ret == GU_UUID_STR_LEN);
        uuid_buf[GU_UUID_STR_LEN] = '\0';

        return (os << uuid_buf);
    }

    inline void string2uuid (const std::string& str, wsrep_uuid_t& uuid)
    {
        ssize_t ret(gu_uuid_scan(str.c_str(), str.length(),
                                 reinterpret_cast<gu_uuid_t*>(&uuid)));

        if (ret == -1)
            gu_throw_error(EINVAL) << "could not parse UUID from '" << str
                                   << '\'' ;
    }

    inline std::istream& operator>>(std::istream& is, wsrep_uuid_t& uuid)
    {
        // @todo
        char cstr[GU_UUID_STR_LEN + 1];
        is.width(GU_UUID_STR_LEN + 1);
        is >> cstr;

        string2uuid(cstr, uuid);

        return is;
    }

}

#endif // GALERA_UUID_HPP
