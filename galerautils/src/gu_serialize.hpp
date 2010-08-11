/*
 * Copyright (C) 2009 Codership Oy <info@codership.com>
 */

/*!
 * @file Helper templates for serialization/unserialization.
 * As we are usually working on little endian platforms, integer
 * storage order is little endian.
 *
 * @todo Templates are safe to use with integer types only. Adjust them
 *       to work also with classes that have special serialization
 *       routines.
 * @todo Big endian is not supported yet.
 * @todo Alignment issues.
 */


#ifndef GU_SERIALIZE_HPP
#define GU_SERIALIZE_HPP

#include "gu_throw.hpp"

#include <stdint.h>

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


    template <typename T>
    inline size_t serialize(const T& t, byte_t* buf, size_t buflen, size_t offset)
        throw (Exception)
    {
        if (offset + serial_size(t) > buflen)
            gu_throw_fatal;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        *reinterpret_cast<T*>(buf + offset) = t;
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
        return (offset + serial_size(t));
    }

    template <typename T>
    inline size_t unserialize(const byte_t* buf, size_t buflen, size_t offset, T& t)
    {
        if (offset + serial_size(t) > buflen)
            gu_throw_fatal;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        t = *reinterpret_cast<const T*>(buf + offset);
#elif __BYTE_ORDER == __BIG_ENDIAN
#error "Big endian not supported yet"
#else
#error "Byte order unrecognized"
#endif // __BYTE_ORDER
        return (offset + serial_size(t));
    }

} // namespace gu

#endif // GU_SERIALIZE_HPP
