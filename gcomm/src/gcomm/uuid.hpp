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

    UUID() : uuid(GU_UUID_NIL) {}

    UUID(const void* node, const size_t node_len) :
        uuid()
    {
        gu_uuid_generate(&uuid, node, node_len);
    }

    UUID(const int32_t idx) :
        uuid()
    {
        assert(idx > 0);
        uuid = GU_UUID_NIL;
        memcpy(&uuid, &idx, sizeof(idx));
    }

    static const UUID& nil()
    {
        return uuid_nil;
    }

    size_t unserialize(const gu::byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gu_throw_error (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);

        memcpy(&uuid, buf + offset, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }

    size_t serialize(gu::byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gu_throw_error (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);

        memcpy(buf + offset, &uuid, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }

    static size_t serial_size()
    {
        return sizeof(gu_uuid_t);
    }

    const gu_uuid_t* get_uuid_ptr() const
    {
        return &uuid;
    }

    bool operator<(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid, &cmp.uuid) < 0);
    }

    bool operator==(const UUID& cmp) const
    {
        return (gu_uuid_compare(&uuid, &cmp.uuid) == 0);
    }

    bool older(const UUID& cmp) const
    {
        return (gu_uuid_older(&uuid, &cmp.uuid) > 0);
    }

    std::ostream& to_stream(std::ostream& os) const
    {
        static const char buf[37] = { 0, };
        const uint32_t* i = reinterpret_cast<const uint32_t*>(uuid.data);

        if (i[0] != 0 &&
            memcmp(i + 1, buf, sizeof(uuid) - sizeof(*i)) == 0)
        {
            // if all of UUID is contained in the first 4 bytes
            os << i[0]; // should this be converted to certain endianness?
        }
        else
        {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(uuid.data);

            using namespace std;

            ios_base::fmtflags saved = os.flags();

            os << hex
               << setfill('0') << setw(8) << gu_be32(i[0]) << '-'
               << setfill('0') << setw(4) << gu_be16(s[2]) << '-'
               << setfill('0') << setw(4) << gu_be16(s[3]) << '-'
               << setfill('0') << setw(4) << gu_be16(s[4]) << '-'
               << setfill('0') << setw(4) << gu_be16(s[5])
               << setfill('0') << setw(8) << gu_be32(i[3]);

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

    gu_uuid_t         uuid;
    static const UUID uuid_nil;
    UUID(gu_uuid_t uuid_) : uuid(uuid_) {}
};



inline std::ostream& gcomm::operator<<(std::ostream& os, const UUID& uuid)
{
    return uuid.to_stream (os);
}

#endif // _GCOMM_UUID_HPP_
