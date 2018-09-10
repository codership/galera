// Copyright (C) 2013 Codership Oy <info@codership.com>
/**
 * @file Message digest interface.
 *
 * $Id$
 */

#ifndef GU_DIGEST_HPP
#define GU_DIGEST_HPP

#include "gu_hash.h"
#include "gu_byteswap.hpp"
#include "gu_serializable.hpp"
#include "gu_macros.hpp"

namespace gu
{

/* Just making MMH3 not derive from Digest reduced TrxHandle size from
 * 4560 bytes to 4256. 304 bytes of vtable pointers... */
class MMH3
{
public:

    MMH3 () : ctx_() { gu_mmh128_init (&ctx_); }

    ~MMH3 () {}

    template <typename T> static int
    digest (const void* const in, size_t const size, T& out)
    {
        byte_t tmp[16];
        gu_mmh128(in, size, tmp);
        int const s(std::min(sizeof(T), sizeof(tmp)));
        ::memcpy (&out, tmp, s);
        return s;
    }

    /* experimental */
    template <typename T> static T
    digest (const void* const in, size_t const size)
    {
        switch (sizeof(T))
        {
        case 1:  return gu_mmh128_32(in, size);
        case 2:  return gu_mmh128_32(in, size);
        case 4:  return gu_mmh128_32(in, size);
        case 8:  return gu_mmh128_64(in, size);
        }
        throw;
    }

    void append (const void* const buf, size_t const size)
    {
        gu_mmh128_append (&ctx_, buf, size);
    }

    template <size_t const size>
    int  gather (void* const buf) const
    {
        GU_COMPILE_ASSERT(size >= 16, wrong_buf_size);
        gather16 (buf);
        return 16;
    }

    int  gather (void* const buf, size_t const size) const
    {
        byte_t tmp[16];
        gather16(tmp);
        int const s(std::min(size, sizeof(tmp)));
        ::memcpy (buf, tmp, s);
        return s;
    }

    void     gather16 (void* const buf) const { gu_mmh128_get (&ctx_, buf); }

    uint64_t gather8() const { return gu_mmh128_get64 (&ctx_); }

    uint32_t gather4() const { return gu_mmh128_get32 (&ctx_); }

    // a questionable feature
    template <typename T> int
    operator() (T& out) const { return gather<sizeof(out)>(&out); }

private:

    gu_mmh128_ctx_t ctx_;

}; /* class MMH3 */

template <> inline int
MMH3::digest (const void* const in, size_t const size, uint8_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t const size, uint16_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t const size, uint32_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t const size, uint64_t& out)
{
    out = gu_mmh128_64(in, size); return sizeof(out);
}

template <> inline int
MMH3::gather<8> (void* const out) const
{
    *(static_cast<uint64_t*>(out)) = gather8(); return 8;
}

template <> inline int
MMH3::gather<4> (void* const out) const
{
    *(static_cast<uint32_t*>(out)) = gather4(); return 4;
}

typedef MMH3 Hash;


class FastHash
{
public:

    template <typename T> static int
    digest (const void* const in, size_t const size, T& out)
    {
        byte_t tmp[16];
        gu_fast_hash128(in, size, tmp);
        int const s(std::min(sizeof(T), sizeof(tmp)));
        ::memcpy (&out, tmp, s);
        return s;
    }

    /* experimental */
    template <typename T> static T
    digest (const void* const in, size_t const size);
    /* The above is undefined and should cause linking error in case that
     * template gets instantiated instead of specialized ones below.
     * Unfortunately GU_COMPILE_ASSERT() is unusable here - causes compilation
     * errors in every unit that only includes this header (probably because
     * method is static).
     * Perhaps templating the class would have done the trick */

}; /* FastHash */

template <> inline int
FastHash::digest (const void* const in, size_t const size, uint8_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t const size, uint16_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t const size, uint32_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t const size, uint64_t& out)
{
    out = gu_fast_hash64(in, size);  return sizeof(out);
}

template <> inline uint8_t
FastHash::digest<uint8_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline uint16_t
FastHash::digest<uint16_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline uint32_t
FastHash::digest<uint32_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline uint64_t
FastHash::digest<uint64_t>(const void* const in, size_t const size)
{
    return gu_fast_hash64(in, size);
}

template <> inline int8_t
FastHash::digest<int8_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline int16_t
FastHash::digest<int16_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline int32_t
FastHash::digest<int32_t>(const void* const in, size_t const size)
{
    return gu_fast_hash32(in, size);
}

template <> inline int64_t
FastHash::digest<int64_t>(const void* const in, size_t const size)
{
    return gu_fast_hash64(in, size);
}

} /* namespace gu */

#endif /* GU_DIGEST_HPP */
