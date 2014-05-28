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

#include "gu_uuid.hpp"

#include <ostream>
#include <iomanip>

namespace gcomm
{
    class UUID;
    std::ostream& operator<<(std::ostream&, const UUID&);
}

class gcomm::UUID :
        public gu::UUID
{
public:

    UUID() : gu::UUID() {}

    UUID(const void* node, const size_t node_len) :
            gu::UUID(node, node_len) {}

    UUID(const int32_t idx) : gu::UUID()
    {
        assert(idx > 0);
        memcpy(&uuid_, &idx, sizeof(idx));
    }

    static const UUID& nil()
    {
        return uuid_nil_;
    }

    std::ostream& to_stream(std::ostream& os, bool full) const
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


            std::ios_base::fmtflags saved = os.flags();
            if (full == true)
            {
                const uint16_t* s(reinterpret_cast<const uint16_t*>(
                                      uuid_.data));
                os << std::hex
                   << std::setfill('0') << std::setw(8) << gu_be32(i[0]) << '-'
                   << std::setfill('0') << std::setw(4) << gu_be16(s[2]) << '-'
                   << std::setfill('0') << std::setw(4) << gu_be16(s[3]) << '-'
                   << std::setfill('0') << std::setw(4) << gu_be16(s[4]) << '-'
                   << std::setfill('0') << std::setw(4) << gu_be16(s[5])
                   << std::setfill('0') << std::setw(8) << gu_be32(i[3]);
            }
            else
            {
                os << std::hex
                   << std::setfill('0') << std::setw(8) << gu_be32(i[0]);
            }
            os.flags(saved);
        }

        return os;
    }

    // Prefer the above function over this one
    std::string full_str() const
    {
        std::ostringstream os;
        to_stream(os, true);
        return os.str();
    }

private:
    static const UUID uuid_nil_;
    UUID(gu_uuid_t uuid) : gu::UUID(uuid) {}
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const UUID& uuid)
{
    return uuid.to_stream (os, false);
}

#endif // _GCOMM_UUID_HPP_
