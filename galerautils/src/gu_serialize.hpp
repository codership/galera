/*
 * Copyright (C) 2009-2017 Codership Oy <info@codership.com>
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

#include "gu_exception.hpp"
#include "gu_byteswap.hpp"
#include "gu_buffer.hpp"
#include "gu_macros.hpp"
#include "gu_utils.hpp"

#include <limits>
#include <cstring> // ::memcpy()

namespace gu
{

    template <typename T>
    inline size_t serial_size(const T& t) { return t.serial_size(); }

    template <>
    inline size_t serial_size(const uint8_t& b)  { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint16_t& b) { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint32_t& b) { return sizeof(b); }

    template <>
    inline size_t serial_size(const uint64_t& b) { return sizeof(b); }

    class SerializationException : public Exception
    {
    public:
        SerializationException(size_t ret, size_t buflen);
    };

    /*
     * Non-checking serialization template helpers for cases where buffer size
     * check is redundant
     */
    template <typename TO, typename FROM>
    inline size_t
    serialize_helper(const FROM& f, void* const buf, size_t const offset)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<TO>::is_integer, not_integer1);
        GU_COMPILE_ASSERT(std::numeric_limits<FROM>::is_integer, not_integer2);
        GU_COMPILE_ASSERT(sizeof(FROM) <= sizeof(TO), size_differs);

        TO const tmp(htog<TO>(f));
        ::memcpy(ptr_offset(buf, offset), &tmp, sizeof(tmp));

        return offset + sizeof(tmp);
    }

    template <typename FROM, typename TO>
    inline size_t
    unserialize_helper(const void* const buf, size_t const offset, TO& t)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<TO>::is_integer, not_integer1);
        GU_COMPILE_ASSERT(std::numeric_limits<FROM>::is_integer, not_integer2);
        GU_COMPILE_ASSERT(sizeof(FROM) <= sizeof(TO), size_differs);

        FROM tmp;
        ::memcpy(&tmp, ptr_offset(buf, offset), sizeof(tmp));
        t = gtoh<FROM>(tmp);

        return offset + sizeof(tmp);
    }

    /* General serialization templates for numeric types */
    template <typename FROM>
    GU_FORCE_INLINE size_t
    serialize(const FROM& f, void* const buf, size_t const offset)
    {
        return serialize_helper<FROM, FROM>(f, buf, offset);
    }

    template <typename TO>
    GU_FORCE_INLINE size_t
    unserialize(const void* const buf, size_t const offset, TO& t)
    {
        return unserialize_helper<TO, TO>(buf, offset, t);
    }

    /* The following templates force explicit size serialization/deserialization
     * at compile stage */
    template <typename T>
    GU_FORCE_INLINE size_t serialize1(const T&     t,
                                      void*  const buf,
                                      size_t const offset)
    {
        return serialize_helper<uint8_t>(t, buf, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize1(const void* const buf,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint8_t>(buf, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize2(const T&     t,
                                      void*  const buf,
                                      size_t const offset)
    {
        return serialize_helper<uint16_t>(t, buf, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize2(const void* const buf,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint16_t>(buf, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize4(const T&     t,
                                      void*  const buf,
                                      size_t const offset)
    {
        return serialize_helper<uint32_t>(t, buf, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize4(const void* const buf,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint32_t>(buf, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize8(const T&     t,
                                      void*  const buf,
                                      size_t const offset)
    {
        return serialize_helper<uint64_t>(t, buf, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize8(const void* const buf,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint64_t>(buf, offset, t);
    }

    /*
     * Buffer length checking serialization template helpers
     */
    GU_FORCE_INLINE void
    check_bounds(size_t need, size_t have)
    {
        if (gu_unlikely(need > have))
            throw SerializationException(need, have);
    }

    template <typename TO, typename FROM>
    inline size_t
    serialize_helper(const FROM& f, void* const buf, size_t const buflen,
                     size_t const offset)
    {
        size_t const check(offset + sizeof(TO));

        gu_trace(check_bounds(check, buflen));

        return serialize_helper<TO, FROM>(f, buf, offset);
    }

    template <typename FROM, typename TO>
    inline size_t
    unserialize_helper(const void* const buf, size_t const buflen,
                       size_t const offset, TO& t)
    {
        size_t const check(offset + sizeof(FROM));

        gu_trace(check_bounds(check, buflen));

        return unserialize_helper<FROM, TO>(buf, offset, t);
    }

    /* General serialization templates for numeric types */
    template <typename FROM>
    GU_FORCE_INLINE size_t
    serialize(const FROM& f, void* const buf, size_t const buflen,
              size_t const offset)
    {
        return serialize_helper<FROM, FROM>(f, buf, buflen, offset);
    }

    template <typename TO>
    GU_FORCE_INLINE size_t
    unserialize(const void* const buf, size_t const buflen, size_t const offset,
                TO& t)
    {
        return unserialize_helper<TO, TO>(buf, buflen, offset, t);
    }

    /* The following templates force explicit size serialization/deserialization
     * at compile stage */
    template <typename T>
    GU_FORCE_INLINE size_t serialize1(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return serialize_helper<uint8_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize1(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint8_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize2(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return serialize_helper<uint16_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize2(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint16_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize4(const T&     t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return serialize_helper<uint32_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize4(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint32_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize8(T   const t,
                                      void*  const buf,
                                      size_t const buflen,
                                      size_t const offset)
    {
        return serialize_helper<uint64_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize8(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        T&                t)
    {
        return unserialize_helper<uint64_t>(buf, buflen, offset, t);
    }

    /*
     * Templates to serialize arbitrary length buffers
     */
    class RepresentationException : public Exception
    {
    public:
        RepresentationException(size_t need, size_t have);
    };

    template <typename ST>
    inline size_t serial_size_helper(const Buffer& sb)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<ST>::is_integer, must_be_integer);

        if (gu_unlikely(sb.size() > std::numeric_limits<ST>::max()))
            throw RepresentationException(sb.size(), sizeof(ST));

        return sizeof(ST) + sb.size();
    }

    GU_FORCE_INLINE size_t serial_size1(const Buffer& sb)
    {
        return serial_size_helper<uint8_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size2(const Buffer& sb)
    {
        return serial_size_helper<uint16_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size4(const Buffer& sb)
    {
        return serial_size_helper<uint32_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size8(const Buffer& sb)
    {
        return serial_size_helper<uint64_t>(sb);
    }

    template <typename ST>
    inline size_t serialize_helper(const Buffer& b,
                                   void*   const buf,
                                   size_t  const buflen,
                                   size_t        offset)
    {
        size_t const ret(offset + serial_size_helper<ST>(b));

        gu_trace(check_bounds(ret, buflen));

        offset = serialize_helper<ST>(static_cast<ST>(b.size()),
                                      buf, buflen, offset);

        // can't use void* in std::copy()
        byte_t* const ptr(static_cast<byte_t*>(buf));
        std::copy(b.begin(), b.end(), ptr + offset);
        return ret;
    }

    template <typename ST>
    inline size_t unserialize_helper(const void* const buf,
                                     size_t      const buflen,
                                     size_t            offset,
                                     Buffer&           b)
    {
        GU_COMPILE_ASSERT(std::numeric_limits<ST>::is_integer, must_be_integer);
        ST len(0);
        size_t ret(offset + sizeof(len));

        gu_trace(check_bounds(ret, buflen));

        offset = unserialize_helper<ST>(buf, buflen, offset, len);
        ret += len;

        gu_trace(check_bounds(ret, buflen));

        b.resize(len);

        // can't use void* in std::copy()
        const byte_t* const ptr(static_cast<const byte_t*>(buf));
        std::copy(ptr + offset, ptr + ret, b.begin());

        return ret;
    }

    GU_FORCE_INLINE size_t serialize1(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return serialize_helper<uint8_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize1(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return unserialize_helper<uint8_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize2(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return serialize_helper<uint16_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize2(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return unserialize_helper<uint16_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize4(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return serialize_helper<uint32_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize4(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return unserialize_helper<uint32_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize8(const Buffer& b,
                                      void*   const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
    {
        return serialize_helper<uint64_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize8(const void* const buf,
                                        size_t      const buflen,
                                        size_t      const offset,
                                        Buffer&           b)
    {
        return unserialize_helper<uint64_t>(buf, buflen, offset, b);
    }

} // namespace gu

#endif // GU_SERIALIZE_HPP
