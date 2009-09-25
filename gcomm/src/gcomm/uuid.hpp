#ifndef _GCOMM_UUID_HPP_
#define _GCOMM_UUID_HPP_

#include <cstring>

#include <gcomm/common.hpp>
#include <gcomm/exception.hpp>
#include <gcomm/string.hpp>
#include <gcomm/types.hpp>

extern "C" {
#include <stddef.h>
#include <gu_uuid.h>
}

BEGIN_GCOMM_NAMESPACE

class UUID
{
    gu_uuid_t uuid;
    static const UUID uuid_nil;
    UUID(gu_uuid_t uuid_) :
        uuid(uuid_)
    {
    }
public:
    UUID() :
        uuid()
    {
        uuid = GU_UUID_NIL;
    }
    
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

    size_t read (const byte_t* buf, const size_t buflen, const size_t offset)
        throw (gu::Exception)
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gcomm_throw_runtime (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);
            
        memcpy(&uuid, buf + offset, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }
    
    size_t write(byte_t* buf, const size_t buflen, const size_t offset) const
        throw (gu::Exception)
    {
        if (buflen < offset + sizeof(gu_uuid_t))
            gcomm_throw_runtime (EMSGSIZE) << sizeof(gu_uuid_t) << " > "
                                           << (buflen - offset);

        memcpy(buf + offset, &uuid, sizeof(gu_uuid_t));

        return offset + sizeof(gu_uuid_t);
    }
    
    static size_t size() 
    {
        return sizeof(gu_uuid_t);
    }
    
    const gu_uuid_t* get_uuid_ptr() const 
    {
        return &uuid;
    }
    
    string to_string() const {
        char buf[37];
        memset(buf, 0, sizeof(buf));

        const int32_t* val = reinterpret_cast<const int32_t*>(&uuid);
        if (*val != 0 &&
            memcmp(val + 1, buf, sizeof(uuid) - sizeof(int32_t)) == 0)
        {
            // if all of UUID is contained in the first 4 bytes
            return gu::to_string(*val);
        }
        else
        {
#define GU_CPP_UUID_FORMAT \
          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
            int const ret = snprintf(buf, sizeof(buf), GU_CPP_UUID_FORMAT,
                                     GU_UUID_ARGS(&uuid));
            // @todo: this is a rather unwarranted check
            if (36 != ret)
            {
                gcomm_throw_fatal << "Failed to print UUID";
            }
#undef GU_CPP_UUID_FORMAT
            return std::string(buf);
        }
    }
};



static inline bool operator==(const UUID& a, const UUID& b)
{
    return gu_uuid_compare(a.get_uuid_ptr(), b.get_uuid_ptr()) == 0;
}

static inline bool operator!=(const UUID& a, const UUID& b)
{
    return !(a == b);
}

static inline bool operator<(const UUID& a, const UUID& b)
{
    return gu_uuid_compare(a.get_uuid_ptr(), b.get_uuid_ptr()) < 0;
}

END_GCOMM_NAMESPACE

#endif // _GCOMM_UUID_HPP_
