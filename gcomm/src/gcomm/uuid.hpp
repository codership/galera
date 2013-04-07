/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

#ifndef _GCOMM_UUID_HPP_
#define _GCOMM_UUID_HPP_

#include "gcomm/exception.hpp"
#include "gcomm/types.hpp"

#include "gu_utils.hpp"
#include "gu_assert.hpp"
#include "gu_byteswap.h"

#include "gu_uuid.h"

#include <ostream>
#include <iomanip>

namespace gcomm
{
    class UUID;
    std::ostream& operator<<(std::ostream&, const UUID&);
}

class gcomm::UUID
{
public:

    UUID() : uuid_(GU_UUID_NIL) {}

    UUID(const void* node, const size_t node_len) : uuid_()
    {
        gu_uuid_generate(&uuid_, node, node_len);
    }

    UUID(const int32_t idx) : uuid_()
    {
        assert(idx > 0);
        uuid_ = GU_UUID_NIL;
        memcpy(&uuid_, &idx, sizeof(idx));
    }

    static const UUID& nil()
    {
        return uuid_nil_;
    }

    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gu_throw_error (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);

        memcpy(&uuid_, buf + offset, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }

    size_t serialize(gu::byte_t* buf, const size_t buflen, const size_t offset) const
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gu_throw_error (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);

        memcpy(buf + offset, &uuid_, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }

    static size_t serial_size()
    {
        return sizeof(gu_uuid_t);
    }

    const gu_uuid_t* uuid_ptr() const
    {
        return &uuid_;
    }

    bool operator<(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) < 0);
    }

    bool operator==(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid_, &cmp.uuid_) == 0);
    }

    bool older(const UUID& cmp) const
    {
        return (gu_uuid_older(&uuid_, &cmp.uuid_) > 0);
    }

    std::ostream& to_stream(std::ostream& os) const
    {
        static const char buf[37] = { 0, };
        const uint32_t* i = reinterpret_cast<const uint32_t*>(uuid_.data);

        if (i[0] != 0 &&
            memcmp(i + 1, buf, sizeof(uuid_) - sizeof(*i)) == 0)
        {
            // if all of UUID is contained in the first 4 bytes
            os << i[0]; // should this be converted to certain endianness?
        }
        else
        {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(uuid_.data);

            std::ios_base::fmtflags saved = os.flags();

            os << std::hex
               << std::setfill('0') << std::setw(8) << gu_be32(i[0]) << '-'
               << std::setfill('0') << std::setw(4) << gu_be16(s[2]) << '-'
               << std::setfill('0') << std::setw(4) << gu_be16(s[3]) << '-'
               << std::setfill('0') << std::setw(4) << gu_be16(s[4]) << '-'
               << std::setfill('0') << std::setw(4) << gu_be16(s[5])
               << std::setfill('0') << std::setw(8) << gu_be32(i[3]);

            os.flags(saved);
        }

        return os;
    }

    // Prefer the above function over this one
    std::string _str() const
    {
        std::ostringstream os;
        to_stream(os);
        return os.str();
    }

private:

    gu_uuid_t         uuid_;
    static const UUID uuid_nil_;
    UUID(gu_uuid_t uuid) : uuid_(uuid) {}
};



inline std::ostream& gcomm::operator<<(std::ostream& os, const UUID& uuid)
{
    return uuid.to_stream (os);
}

#endif // _GCOMM_UUID_HPP_
