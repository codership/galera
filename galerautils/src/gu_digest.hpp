// Copyright (C) 2013 Codership Oy <info@codership.com>
/**
 * @file Message digest interface.
 *
 * $Id$
 */

#ifndef GU_DIGEST_HPP
#define GU_DIGEST_HPP

#include "gu_hash.h"
#include "gu_vec16.h"
#include "gu_byteswap.hpp"
#include "gu_serializable.hpp"

namespace gu
{

class Digest : public Serializable
{
public:

    /* size of digest in bytes */
    Digest (size_t size) : Serializable(), size_(size) {}

    void
    append (const void* const buf, size_t const size)
    {
        my_append (buf, size);
    }

    void
    append (const std::vector<byte_t>& v, size_t const offset)
    {
        append (&v[offset], v.size() - offset);
    }

    void
    append (const std::vector<byte_t>& v) { append (v, 0); }

    template <typename T> void
    append (const T& obj) { append (&obj, sizeof(obj)); }

    int
    gather (void* const out, ssize_t size) const
    {
        switch (size)
        {
        case 16: my_gather16 (out);
            break;
        case 8: *(reinterpret_cast<uint64_t*>(out)) = htog64 (my_gather8());
            break;
        case 4: *(reinterpret_cast<uint32_t*>(out)) = htog32 (my_gather4());
            break;
        default: size = my_gather (out, size);
        }

        return size;
    }

    template <typename T> int
    gather (T& out) const { return my_gather(&out, sizeof(T)); }

    template <typename T> int
    operator() (T& out) const { return gather(out); }

protected:

    ~Digest () {}

private:

    char const size_; // max digest size in bytes. cannot be more than 32

    virtual void     my_append   (const void* in, size_t size) = 0;

    virtual int      my_gather   (void* out, size_t size) const = 0;

    virtual void     my_gather16 (void* out) const = 0;

    virtual uint64_t my_gather8  ()          const = 0;

    virtual uint32_t my_gather4  ()          const = 0;

    ssize_t  my_serial_size  () const { return size_; }

    ssize_t  my_serialize_to (void* buf, ssize_t size) const
    {
        return gather (buf, size);
    }
};

template <> inline int
Digest::gather (uint8_t&  out) const { out = my_gather4(); return sizeof(out); }

template <> inline int
Digest::gather (uint16_t& out) const { out = my_gather4(); return sizeof(out); }

template <> inline int
Digest::gather (uint32_t& out) const { out = my_gather4(); return sizeof(out); }

template <> inline int
Digest::gather (uint64_t& out) const { out = my_gather8(); return sizeof(out); }


class MMH3 : public Digest
{
public:

    MMH3 () : Digest(16), ctx_() { gu_mmh128_init (&ctx_); }

    virtual ~MMH3 () {}

    template <typename T> static int
    digest (const void* const in, size_t size, T& out)
    {
        byte_t tmp[16];
        gu_mmh128(in, size, tmp);
        int const s(std::min(sizeof(T), sizeof(tmp)));
        ::memcpy (&out, tmp, s);
        return s;
    }

    /* experimental */
    template <typename T> static T
    digest (const void* const in, size_t size)
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

private:

    gu_mmh128_ctx_t ctx_;

    void my_append (const void* const buf, size_t const size)
    {
        gu_mmh128_append (&ctx_, buf, size);
    }

    void     my_gather16 (void* const buf) const { gu_mmh128_get (&ctx_, buf); }

    uint64_t my_gather8() const { return gu_mmh128_get64 (&ctx_);}

    uint32_t my_gather4() const { return gu_mmh128_get32 (&ctx_);}

    int      my_gather (void* const buf, size_t const size) const
    {
        byte_t tmp[16];
        my_gather16 (tmp);
        int const s(std::min(size, sizeof(tmp)));
        ::memcpy (buf, tmp, s);
        return s;
    }
};

template <> inline int
MMH3::digest (const void* const in, size_t size, uint8_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t size, uint16_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t size, uint32_t& out)
{
    out = gu_mmh128_32(in, size); return sizeof(out);
}

template <> inline int
MMH3::digest (const void* const in, size_t size, uint64_t& out)
{
    out = gu_mmh128_64(in, size); return sizeof(out);
}


typedef MMH3 Hash;

class FastHash
{
public:

    template <typename T> static int
    digest (const void* const in, size_t size, T& out)
    {
        byte_t tmp[16];
        gu_fast_hash128(in, size, tmp);
        int const s(std::min(sizeof(T), sizeof(tmp)));
        ::memcpy (&out, tmp, s);
        return s;
    }

    /* experimental */
    template <typename T> static T
    digest (const void* const in, size_t size)
    {
        switch (sizeof(T))
        {
        case 1:  return gu_fast_hash32(in, size);
        case 2:  return gu_fast_hash32(in, size);
        case 4:  return gu_fast_hash32(in, size);
        case 8:  return gu_fast_hash64(in, size);
        }
        throw;
    }
};

template <> inline int
FastHash::digest (const void* const in, size_t size, uint8_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t size, uint16_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t size, uint32_t& out)
{
    out = gu_fast_hash32(in, size);  return sizeof(out);
}

template <> inline int
FastHash::digest (const void* const in, size_t size, uint64_t& out)
{
    out = gu_fast_hash64(in, size);  return sizeof(out);
}

typedef FastHash CRC;

} /* namespace gu */

#endif /* GU_DIGEST_HPP */
