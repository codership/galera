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

#include <boost/static_assert.hpp>
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
    __do_not_use_serialize(const FROM& f,
                           byte_t* const buf, size_t const buflen, size_t const offset)
        throw (Exception)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<TO>::is_integer);
        BOOST_STATIC_ASSERT(std::numeric_limits<FROM>::is_integer);
        BOOST_STATIC_ASSERT(sizeof(FROM) == sizeof(TO));
        size_t const ret = offset + sizeof(TO);
        if (gu_unlikely(ret > buflen)) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        *reinterpret_cast<TO*>(buf + offset) = htog<TO>(f);
        return ret;
    }

    /* Should not be used directly! */
    template <typename FROM, typename TO>
    inline size_t
    __do_not_use_unserialize(const byte_t* const buf, size_t const buflen, size_t const offset,
                             TO& t)
        throw (Exception)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<TO>::is_integer);
        BOOST_STATIC_ASSERT(std::numeric_limits<FROM>::is_integer);
        BOOST_STATIC_ASSERT(sizeof(FROM) == sizeof(TO));
        size_t const ret = offset + sizeof(t);
        if (gu_unlikely(ret > buflen)) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        t = gtoh<FROM>(*reinterpret_cast<const FROM*>(buf + offset));
        return ret;
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize1(const T& t,
                                      byte_t* const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
        throw (Exception)
    {
        return __do_not_use_serialize<uint8_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize1(const byte_t* const buf,
                                        size_t const buflen,
                                        size_t const offset,
                                        T& t)
        throw (Exception)
    {
        return __do_not_use_unserialize<uint8_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize2(const T& t,
                                      byte_t* const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
        throw (Exception)
    {
        return __do_not_use_serialize<uint16_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize2(const byte_t* const buf,
                                        size_t const buflen,
                                        size_t const offset,
                                        T& t)
        throw (Exception)
    {
        return __do_not_use_unserialize<uint16_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize4(const T& t,
                                      byte_t* const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
        throw (Exception)
    {
        return __do_not_use_serialize<uint32_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize4(const byte_t* const buf,
                                        size_t const buflen,
                                        size_t const offset,
                                        T& t)
        throw (Exception)
    {
        return __do_not_use_unserialize<uint32_t>(buf, buflen, offset, t);
    }

    template <typename T>
    GU_FORCE_INLINE size_t serialize8(const T& t,
                                      byte_t* const buf,
                                      size_t  const buflen,
                                      size_t  const offset)
        throw (Exception)
    {
        return __do_not_use_serialize<uint64_t>(t, buf, buflen, offset);
    }

    template <typename T>
    GU_FORCE_INLINE size_t unserialize8(const byte_t* const buf,
                                        size_t const buflen,
                                        size_t const offset,
                                        T& t)
        throw (Exception)
    {
        return __do_not_use_unserialize<uint64_t>(buf, buflen, offset, t);
    }

    template <typename ST>
    inline size_t __do_not_use_serial_size(const gu::Buffer& sb)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<ST>::is_integer);
        if (sb.size() > std::numeric_limits<ST>::max())
            gu_throw_error(ERANGE) << sb.size() << " unrepresentable in "
                                   << sizeof(ST) << " bytes.";
        return sizeof(ST) + sb.size();
    }

    GU_FORCE_INLINE size_t serial_size1(const gu::Buffer& sb)
    {
        return __do_not_use_serial_size<uint8_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size2(const gu::Buffer& sb)
    {
        return __do_not_use_serial_size<uint16_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size4(const gu::Buffer& sb)
    {
        return __do_not_use_serial_size<uint32_t>(sb);
    }

    GU_FORCE_INLINE size_t serial_size8(const gu::Buffer& sb)
    {
        return __do_not_use_serial_size<uint64_t>(sb);
    }

    template <typename ST>
    inline size_t __do_not_use_serialize(const gu::Buffer& b,
                                         gu::byte_t* const buf,
                                         size_t      const buflen,
                                         size_t            offset)
    {
        size_t const ret = offset + __do_not_use_serial_size<ST>(b);
        if (ret > buflen) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;
        offset = __do_not_use_serialize<ST>(static_cast<ST>(b.size()), buf, buflen, offset);
        copy(b.begin(), b.end(), buf + offset);
        return ret;
    }

    template <typename ST>
    inline size_t __do_not_use_unserialize(const gu::byte_t* const buf,
                                           size_t            const buflen,
                                           size_t                  offset,
                                           gu::Buffer&             b)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<ST>::is_integer);
        ST len(0);
        size_t ret = offset + sizeof(len);
        if (ret > buflen) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;

        offset = __do_not_use_unserialize<ST>(buf, buflen, offset, len);
        ret += len;
        if (ret > buflen) gu_throw_error(EMSGSIZE) << ret << " > " << buflen;

        b.resize(len);
        copy(buf + offset, buf + ret, b.begin());
        return ret;
    }

    GU_FORCE_INLINE size_t serialize1(const gu::Buffer& b,
                                      gu::byte_t* const buf,
                                      size_t      const buflen,
                                      size_t      const offset)
    {
        return __do_not_use_serialize<uint8_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize1(const gu::byte_t* const buf,
                                        size_t            const buflen,
                                        size_t            const offset,
                                        gu::Buffer&             b)
    {
        return __do_not_use_unserialize<uint8_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize2(const gu::Buffer& b,
                                      gu::byte_t* const buf,
                                      size_t      const buflen,
                                      size_t      const offset)
    {
        return __do_not_use_serialize<uint16_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize2(const gu::byte_t* const buf,
                                        size_t            const buflen,
                                        size_t            const offset,
                                        gu::Buffer&             b)
    {
        return __do_not_use_unserialize<uint16_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize4(const gu::Buffer& b,
                                      gu::byte_t* const buf,
                                      size_t      const buflen,
                                      size_t      const offset)
    {
        return __do_not_use_serialize<uint32_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize4(const gu::byte_t* const buf,
                                        size_t            const buflen,
                                        size_t            const offset,
                                        gu::Buffer&             b)
    {
        return __do_not_use_unserialize<uint32_t>(buf, buflen, offset, b);
    }

    GU_FORCE_INLINE size_t serialize8(const gu::Buffer& b,
                                      gu::byte_t* const buf,
                                      size_t      const buflen,
                                      size_t      const offset)
    {
        return __do_not_use_serialize<uint64_t>(b, buf, buflen, offset);
    }

    GU_FORCE_INLINE size_t unserialize8(const gu::byte_t* const buf,
                                        size_t            const buflen,
                                        size_t            const offset,
                                        gu::Buffer&             b)
    {
        return __do_not_use_unserialize<uint64_t>(buf, buflen, offset, b);
    }

} // namespace gu

#endif // GU_SERIALIZE_HPP
