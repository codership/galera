/*
 * Copyright (C) 2014-2017 Codership Oy <info@codership.com>
 *
 */

#ifndef _gu_uuid_hpp_
#define _gu_uuid_hpp_

#include "gu_uuid.h"
#include "gu_arch.h"        // GU_ASSERT_ALIGNMENT
#include "gu_assert.hpp"
#include "gu_macros.hpp"
#include "gu_buffer.hpp"
#include "gu_exception.hpp"
#include "gu_serialize.hpp" // check_range()
#include "gu_utils.hpp"     // ptr_offset()

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

    UUID_base(const void* const node, const size_t node_len) : uuid_()
    {
        gu_uuid_generate(&uuid_, node, node_len);
    }

    UUID_base(gu_uuid_t uuid) : uuid_(uuid) {}

    class SerializeException : public Exception
    {
    public:
        SerializeException(size_t need, size_t have);
    };

    static size_t serial_size()
    {
        return sizeof(UUID_base().uuid_);
    }

    size_t unserialize(const void* const buf, const size_t offset)
    {
        size_t const len(serial_size());
        ::memcpy(&uuid_, ptr_offset(buf, offset), len);
        return offset + len;
    }

    size_t serialize  (void* const buf, const size_t offset) const
    {
        size_t const len(serial_size());
        ::memcpy(ptr_offset(buf, offset), &uuid_, len);
        return offset + len;
    }

    size_t unserialize(const void* const buf, const size_t buflen,
                       const size_t offset)
    {
        gu_trace(gu::check_bounds(offset + serial_size(), buflen));
        return unserialize(buf, offset);
    }

    size_t serialize  (void* const buf, const size_t buflen,
                       const size_t offset) const
    {
        gu_trace(gu::check_bounds(offset + serial_size(), buflen));
        return serialize(buf, offset);
    }

    const gu_uuid_t* ptr() const
    {
        return &uuid_;
    }

    GU_FORCE_INLINE
    UUID_base& operator=(const UUID_base& u)
    {
        gu_uuid_copy(&uuid_, &u.uuid_);
        return *this;
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
