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
        public gu::UUID_base
{
public:

    UUID() : gu::UUID_base() {}

    UUID(const void* node, const size_t node_len) :
            gu::UUID_base(node, node_len) {}

    UUID(const int32_t idx) : gu::UUID_base()
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
        std::ios_base::fmtflags saved = os.flags();
        if (full == true)
        {
            os << uuid_;
        }
        else
        {
            os << std::hex
               << std::setfill('0') << std::setw(2)
               << static_cast<int>(uuid_.data[0])
               << std::setfill('0') << std::setw(2)
               << static_cast<int>(uuid_.data[1])
               << std::setfill('0') << std::setw(2)
               << static_cast<int>(uuid_.data[2])
               << std::setfill('0') << std::setw(2)
               << static_cast<int>(uuid_.data[3]);
        }
        os.flags(saved);
        return os;
    }

    // Prefer the above function over this one
    std::string full_str() const
    {
        std::ostringstream os;
        to_stream(os, true);
        return os.str();
    }

    void increment_incarnation()
    {
        uint16_t* data = reinterpret_cast<uint16_t*>(uuid_.data);
        uint16_t  inc  = gu_be16(data[4]);
        inc++;
        data[4] = gu_be16(inc);
    }

    bool fixed_part_matches(const gcomm::UUID& uuid) const
    {
        return ((memcmp(uuid_.data, uuid.ptr()->data, 8) == 0) &&
                (memcmp(uuid_.data+10, uuid.ptr()->data+10, 6) == 0));
    }

private:
    static const UUID uuid_nil_;
    UUID(gu_uuid_t uuid) : gu::UUID_base(uuid) {}
};

inline std::ostream& gcomm::operator<<(std::ostream& os, const UUID& uuid)
{
    return uuid.to_stream (os, false);
}

#endif // _GCOMM_UUID_HPP_
