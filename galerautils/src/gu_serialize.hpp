/*
 * Copyright (C) 2009-2012 Codership Oy <info@codership.com>
 */

/*!
 * @file Helper templates for serialization/unserialization.
 * As we are usually working on little endian platforms, integer
 * storage order is little-endian - in other words we use "Galera"
 * order, which is by default little-endian.
 *
 * What is going on down there? Templates are good. However we do
 * not serialize the value of size_t variable into sizeof(size_t)
 * bytes. We serialize it into a globally consistent, fixed number
 * of bytes, regardless of the local size of size_t variable.
 *
 * Hence templating by the source variable size should not be used.
 * Instead there are functions/templates that serialize to an explicit
 * number of bytes.
 *
 * @todo Templates are safe to use with integer types only. Adjust them
 *       to work also with classes that have special serialization
 *       routines.
 * @todo Make buffer serialization functions Buffer class methods.
 * @todo Alignment issues.
 */


#ifndef GU_SERIALIZE_HPP
#define GU_SERIALIZE_HPP

#include "gu_throw.hpp"
#include "gu_byteswap.hpp"
#include "gu_buffer.hpp"
#include "gu_macros.hpp"

#include <limits>

namespace gu
{
    template <typename T>
    inline size_t serial_size(const T& t)
    { return t.serial_size(); }

    template <>
    inline size_t serial_size(const uint8_t& b)
    { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint16_t& b)
    { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint32_t& b)
    { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint64_t& b)
    { return sizeof(b); }

    /* Should not be used directly! */
    template <typename TO, typename FROM>
    inline size_t
    __private_serialize(const FROM& f, void* const buf, size_t const buflen,
                        size_t const offset)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<TO>::is_integer, not_integer1);
        GU_COMPILE_ASSERT(std::numeric_limits<FROM>::is_integer, not_integer2);
        GU_COMPILE_ASSERT(sizeof(FROM) == sizeof(TO), size_differs);
        size_t const ret = offset + sizeof(TO);
        if (gu_unlikely(ret > buflen))
        {
            gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        }
        void* const pos(reinterpret_cast<byte_t*>(buf) + offset);
        *reinterpret_cast<TO*>(pos) = htog<TO>(f);
        return ret;
    }

    /* Should not be used directly! */
    template <typename FROM, typename TO>
    inline size_t
    __private_unserialize(const void* const buf, size_t const buflen,
                          size_t const offset, TO& t)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<TO>::is_integer, not_integer1);
        GU_COMPILE_ASSERT(std::numeric_limits<FROM>::is_integer, not_integer2);
        GU_COMPILE_ASSERT(sizeof(FROM) == sizeof(TO), size_differs);
        size_t const ret = offset + sizeof(t);
        if (gu_unlikely(ret > buflen))
        {
            gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        }
        const void* const pos(reinterpret_cast<const byte_t*>(buf) + offset);
        t = gtoh<FROM>(*reinterpret_cast<const FROM*>(pos));
        return ret;
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize1(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return __private_serialize<uint8_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize1(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return __private_unserialize<uint8_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize2(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return __private_serialize<uint16_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize2(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return __private_unserialize<uint16_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize4(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return __private_serialize<uint32_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize4(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return __private_unserialize<uint32_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize8(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return __private_serialize<uint64_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize8(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return __private_unserialize<uint64_t>(buf, buflen, offset, t);
    }

    template <typename ST>
    inline size_t __private_serial_size(const Buffer& sb)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<ST>::is_integer, must_be_integer);
        if (sb.size() > std::numeric_limits<ST>::max())
            gu_throw_error(ERANGE) << sb.size() << " unrepresentable in "
                                   << sizeof(ST) << " bytes.";
        return sizeof(ST) + sb.size();
    }

    GU_FORCE_INLINE size_t serial_size1(const Buffer& sb)
    {
        return __private_serial_size<uint8_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size2(const Buffer& sb)
    {
        return __private_serial_size<uint16_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size4(const Buffer& sb)
    {
        return __private_serial_size<uint32_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size8(const Buffer& sb)
    {
        return __private_serial_size<uint64_t>(sb);
    }

    template <typename ST>
    inline size_t __private_serialize(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t        offset)
    {
        size_t const ret = offset + __private_serial_size<ST>(b);

        if (ret > buflen)
        {
            gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        }

        offset = __private_serialize<ST>(static_cast<ST>(b.size()),
                                         buf, buflen, offset);
        copy(b.begin(), b.end(), reinterpret_cast<byte_t*>(buf) + offset);
        return ret;
    }

    template <typename ST>
    inline size_t __private_unserialize(const void* const buf,
                                        size_t      const buflen,
                                        size_t            offset,
                                        Buffer&           b)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<ST>::is_integer, must_be_integer);
        ST len(0);
        size_t ret = offset + sizeof(len);

        if (ret > buflen) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;

        offset = __private_unserialize<ST>(buf, buflen, offset, len);
        ret += len;

        if (ret > buflen) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;

        b.resize(len);
        const byte_t* const ptr(reinterpret_cast<const byte_t*>(buf));
        copy(ptr + offset, ptr + ret, b.begin());

        return ret;
    }

    GU_FORCE_INLINE size_t serialize1(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return __private_serialize<uint8_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize1(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return __private_unserialize<uint8_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize2(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return __private_serialize<uint16_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize2(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return __private_unserialize<uint16_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize4(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return __private_serialize<uint32_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize4(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return __private_unserialize<uint32_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize8(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return __private_serialize<uint64_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize8(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return __private_unserialize<uint64_t>(buf, buflen, offset, b);
    }

} // namespace gu

#endif // GU_SERIALIZE_HPP
