/*
 * Copyright (C) 2014-2017 Codership Oy <info@codership.com>
 *
 */

#ifndef _gu_uuid_hpp_
#define _gu_uuid_hpp_

#include "gu_uuid.h"
#include "gu_arch.h" // GU_ASSERT_ALIGNMENT
#include "gu_assert.hpp"
#include "gu_buffer.hpp"
#include "gu_exception.hpp"

#include <istream>
#include <cstring>

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

namespace gu {
    class UUIDScanException : public Exception
    {
    public:
        UUIDScanException(const std::string& s);
    };
}

inline ssize_t gu_uuid_from_string(const std::string& s, gu_uuid_t& uuid)
{
    ssize_t ret(gu_uuid_scan(s.c_str(), s.size(), &uuid));
    if (gu_unlikely(ret == -1)) throw gu::UUIDScanException(s);
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

GU_FORCE_INLINE size_t gu_uuid_serial_size(const gu_uuid_t& uuid)
{
    return sizeof(uuid.data);
}

namespace gu {
    class UUIDSerializeException : public Exception
    {
    public:
        UUIDSerializeException(size_t need, size_t have);
    };
}

inline size_t gu_uuid_serialize(const gu_uuid_t& uuid, gu::byte_t* buf,
                                size_t const buflen, size_t const offset)
{
    size_t const len(gu_uuid_serial_size(uuid));
    size_t const end_offset(offset + len);

    if (gu_unlikely(end_offset > buflen))
        throw gu::UUIDSerializeException(len, buflen - offset);

    ::memcpy(buf + offset, uuid.data, len);
    return end_offset;
}

inline size_t gu_uuid_unserialize(const gu::byte_t* buf, size_t const buflen,
                                  size_t const offset, gu_uuid_t& uuid)
{
    size_t const len(gu_uuid_serial_size(uuid));
    size_t const end_offset(offset + len);

    if (gu_unlikely(end_offset > buflen))
        throw gu::UUIDSerializeException(len, buflen - offset);

    ::memcpy(uuid.data, buf + offset, len);
    return end_offset;
}

namespace gu {
    class UUID;
}

class gu::UUID
{
public:

    UUID() : uuid_(GU_UUID_NIL) {}

    UUID(const void* node, const size_t node_len) : uuid_()
    {
        gu_uuid_generate(&uuid_, node, node_len);
    }

    UUID(gu_uuid_t uuid) : uuid_(uuid) {}

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

    GU_FORCE_INLINE
    UUID& operator=(const UUID& u)
    {
        gu_uuid_copy(&uuid_, &u.uuid_);
        return *this;
    }

    bool operator<(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) < 0);
    }

    bool operator==(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) == 0);
    }

    bool operator!=(const UUID& cmp) const
    {
        return !(*this == cmp);
    }

    bool older(const UUID& cmp) const
    {
        return (gu_uuid_older(&uuid_, &cmp.uuid_) > 0);
    }

    void write_stream(std::ostream& os) const
    {
        os << uuid_;
    }

    void read_stream(std::istream& is)
    {
        is >> uuid_;
    }

protected:
    gu_uuid_t         uuid_;
}; // class UUID

namespace gu
{
inline std::ostream& operator<<(std::ostream& os, const gu::UUID& uuid)
{
    uuid.write_stream(os);
    return os;
}

inline std::istream& operator>>(std::istream& is, gu::UUID& uuid)
{
    uuid.read_stream(is);
    return is;
}
} // namespace gu

#endif // _gu_uuid_hpp_
