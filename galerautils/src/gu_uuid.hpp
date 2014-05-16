/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 */

#ifndef _gu_uuid_hpp_
#define _gu_uuid_hpp_

#include "gu_uuid.h"
#include "gu_assert.hpp"
#include "gu_buffer.hpp"
#include <iostream>

inline bool operator==(const gu_uuid_t& a, const gu_uuid_t& b)
{
    return gu_uuid_compare(&a, &b) == 0;
}

inline bool operator!=(const gu_uuid_t& a, const gu_uuid_t& b)
{
    return !(a == b);
}

inline std::ostream& operator<<(std::ostream& os, const gu_uuid_t& uuid)
{
    char uuid_buf[GU_UUID_STR_LEN + 1];
    ssize_t ret(gu_uuid_print(&uuid, uuid_buf, sizeof(uuid_buf)));
    (void)ret;
    assert(ret == GU_UUID_STR_LEN);
    uuid_buf[GU_UUID_STR_LEN] = '\0';
    return (os << uuid_buf);
}

inline ssize_t gu_uuid_from_string(const std::string& s, gu_uuid_t& uuid)
{
    ssize_t ret(gu_uuid_scan(s.c_str(), s.size(), &uuid));
    if (ret == -1) {
        gu_throw_error(EINVAL) << "could not parse UUID from '" << s
                               << '\'' ;
    }
    return ret;
}

inline std::istream& operator>>(std::istream& is, gu_uuid_t& uuid)
{
    char str[GU_UUID_STR_LEN + 1];
    is.width(GU_UUID_STR_LEN + 1);
    is >> str;
    gu_uuid_from_string(str, uuid);
    return is;
}

inline size_t gu_uuid_serial_size(const gu_uuid_t& uuid)
{
    return sizeof(uuid.data);
}

inline size_t gu_uuid_serialize(const gu_uuid_t& uuid, gu::byte_t* buf,
                                size_t buflen, size_t offset)
{
    if (offset + gu_uuid_serial_size(uuid) > buflen)
        gu_throw_error (EMSGSIZE) << gu_uuid_serial_size(uuid)
                                  << " > " << (buflen - offset);
    memcpy(buf + offset, uuid.data, gu_uuid_serial_size(uuid));
    offset += gu_uuid_serial_size(uuid);
    return offset;
}

inline size_t gu_uuid_unserialize(const gu::byte_t* buf, size_t buflen,
                                  size_t offset, gu_uuid_t& uuid)
{
    if (offset + gu_uuid_serial_size(uuid) > buflen)
        gu_throw_error (EMSGSIZE) << gu_uuid_serial_size(uuid)
                                  << " > " << (buflen - offset);
    memcpy(uuid.data, buf + offset, gu_uuid_serial_size(uuid));
    offset += gu_uuid_serial_size(uuid);
    return offset;
}


#endif // _gu_uuid_hpp_
