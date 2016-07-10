/*
 * Copyright (C) 2014-2015 Codership Oy <info@codership.com>
 *
 */

#ifndef _gu_uuid_hpp_
#define _gu_uuid_hpp_

#include "gu_uuid.h"
#include "gu_assert.hpp"
#include "gu_types.hpp"
#include "gu_macros.hpp"
#include <iostream>
#include <cstring>
#include <algorithm> // std::copy

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

size_t gu_uuid_from_string(const std::string& s, gu_uuid_t& uuid);

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

inline
size_t gu_uuid_serialize_unchecked(const gu_uuid_t& uuid, void* const buf,
                                   size_t const buflen, size_t const offset)
{
    assert(offset + gu_uuid_serial_size(uuid) <= buflen);

    std::copy(uuid.data, uuid.data + gu_uuid_serial_size(uuid),
              static_cast<char*>(buf) + offset);

    return (offset + gu_uuid_serial_size(uuid));
}

size_t gu_uuid_serialize(const gu_uuid_t& uuid,
                         void* buf, size_t buflen, size_t offset);

inline
size_t gu_uuid_unserialize_unchecked(const void* const buf, size_t const buflen,
                                     size_t const offset, gu_uuid_t& uuid)
{
    assert(offset + gu_uuid_serial_size(uuid) <= buflen);

    const char* const from(static_cast<const char*>(buf) + offset);
    std::copy(from, from + gu_uuid_serial_size(uuid), uuid.data);

    return (offset + gu_uuid_serial_size(uuid));
}

size_t gu_uuid_unserialize(const void* buf, size_t buflen, size_t offset,
                           gu_uuid_t& uuid);

namespace gu {
    class UUID_base;
    class UUID;
}

/* This class should not be used directly. It is here to allow
 * gu::UUID and gcomm::UUID to inherit from it without the virtual table
(* overhead. */
class gu::UUID_base
{
public:

    UUID_base() : uuid_(GU_UUID_NIL) {}

    UUID_base(const void* node, const size_t node_len) : uuid_()
    {
        gu_uuid_generate(&uuid_, node, node_len);
    }

    UUID_base(gu_uuid_t uuid) : uuid_(uuid) {}

    size_t unserialize(const void* buf, const size_t buflen, const size_t offset)
    {
        return gu_uuid_unserialize(buf, buflen, offset, uuid_);
    }

    size_t serialize(void* buf, const size_t buflen, const size_t offset) const
    {
        return gu_uuid_serialize(uuid_, buf, buflen, offset);
    }

    size_t unserialize_unchecked(const void* buf,
                                 const size_t buflen, const size_t offset)
    {
        return gu_uuid_unserialize_unchecked(buf, buflen, offset, uuid_);
    }

    size_t serialize_unchecked(void* buf,
                               const size_t buflen, const size_t offset) const
    {
        return gu_uuid_serialize_unchecked(uuid_, buf, buflen, offset);
    }

    static size_t serial_size()
    {
        return sizeof(gu_uuid_t);
    }

    const gu_uuid_t* ptr() const
    {
        return &uuid_;
    }

    gu_uuid_t* ptr()
    {
        return &uuid_;
    }

    bool operator<(const UUID_base& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) < 0);
    }

    bool operator==(const gu_uuid_t& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp) == 0);
    }

    bool operator!=(const gu_uuid_t& cmp) const
    {
        return !(*this == cmp);
    }

    bool operator==(const UUID_base& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) == 0);
    }

    bool operator!=(const UUID_base& cmp) const
    {
        return !(*this == cmp);
    }

    bool older(const UUID_base& cmp) const
    {
        return (gu_uuid_older(&uuid_, &cmp.uuid_) > 0);
    }

    std::ostream& print(std::ostream& os) const
    {
        return (os << uuid_);
    }

    std::istream& scan(std::istream& is)
    {
        return (is >> uuid_);
    }

    UUID_base& operator=(const gu_uuid_t& other)
    {
        uuid_ = other;
        return *this;
    }

    const gu_uuid_t& operator()() const
    {
        return uuid_;
    }

protected:

    ~UUID_base() {}

    gu_uuid_t uuid_;

private:
    GU_COMPILE_ASSERT(sizeof(gu_uuid_t) == GU_UUID_LEN, UUID_size);
}; /* class UUID_base */

class gu::UUID : public UUID_base
{
public:

    UUID() : UUID_base() {}

    UUID(const void* node, const size_t node_len) : UUID_base(node, node_len)
    {}

    UUID(gu_uuid_t uuid) : UUID_base(uuid) {}
}; /* class UUID */

namespace gu
{
inline std::ostream& operator<< (std::ostream& os, const gu::UUID_base& uuid)
{
    uuid.print(os); return os;
}

inline std::istream& operator>> (std::istream& is, gu::UUID_base& uuid)
{
    uuid.scan(is); return is;
}

} /* namespace gu */

#endif // _gu_uuid_hpp_
