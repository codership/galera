/*
 * Copyright (C) 2014 Codership Oy <info@codership.com>
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

namespace gu {
    class BaseUUID;
}

class gu::BaseUUID
{
public:

    BaseUUID() : uuid_(GU_UUID_NIL) {}

    BaseUUID(const void* node, const size_t node_len) : uuid_()
    {
        gu_uuid_generate(&uuid_, node, node_len);
    }

    BaseUUID(gu_uuid_t uuid) : uuid_(uuid) {}

    virtual ~BaseUUID() {}
    size_t unserialize(const gu::byte_t* buf,
                       const size_t buflen, const size_t offset)
    {
        return gu_uuid_unserialize(buf, buflen, offset, uuid_);
    }

    size_t serialize(gu::byte_t* buf,
                     const size_t buflen, const size_t offset) const
    {
        return gu_uuid_serialize(uuid_, buf, buflen, offset);
    }

    static size_t serial_size()
    {
        return sizeof(gu_uuid_t);
    }

    const gu_uuid_t* uuid_ptr() const
    {
        return &uuid_;
    }

    bool operator<(const BaseUUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) < 0);
    }

    bool operator==(const BaseUUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) == 0);
    }

    bool older(const BaseUUID& cmp) const
    {
        return (gu_uuid_older(&uuid_, &cmp.uuid_) > 0);
    }

    std::ostream& write_stream(std::ostream& os) const
    {
        char uuid_buf[GU_UUID_STR_LEN + 1];
        ssize_t ret(gu_uuid_print(&uuid_, uuid_buf, sizeof(uuid_buf)));
        (void)ret;

        assert(ret == GU_UUID_STR_LEN);
        uuid_buf[GU_UUID_STR_LEN] = '\0';

        return (os << uuid_buf);
    }

    std::istream& read_stream(std::istream& is)
    {
        char str[GU_UUID_STR_LEN + 1];
        is.width(GU_UUID_STR_LEN + 1);
        is >> str;
        ssize_t ret(gu_uuid_scan(str, GU_UUID_STR_LEN, &uuid_));
        if (ret == -1)
            gu_throw_error(EINVAL) << "could not parse BaseUUID from '" << str
                                   << '\'' ;
        return is;
    }

protected:
    gu_uuid_t         uuid_;
}; // class BaseUUID

#endif // _gu_uuid_hpp_
