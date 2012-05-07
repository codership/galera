//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_SERIALIZATION_HPP
#define GALERA_SERIALIZATION_HPP

#include "gu_throw.hpp"
#include "gu_buffer.hpp"

#include <boost/static_assert.hpp>

#include <limits>

namespace galera
{
    // Helper templates to serialize integer types
    template<typename I> size_t serialize(const I& i,
                                          gu::byte_t* buf,
                                          size_t buf_len,
                                          size_t offset)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<I>::is_integer);
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        *reinterpret_cast<I*>(buf + offset) = i;
        return (offset + sizeof(i));
    }

    template<typename I> size_t unserialize(const gu::byte_t* buf,
                                            size_t buf_len,
                                            size_t offset,
                                            I& i)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<I>::is_integer);
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        i = *reinterpret_cast<const I*>(buf + offset);
        return (offset + sizeof(i));
    }

    template<typename I> size_t serial_size(const I& i)
    {
        BOOST_STATIC_ASSERT(std::numeric_limits<I>::is_integer);
        return sizeof(i);
    }

    template<typename ST>
    size_t serialize(const gu::Buffer& b,
                     gu::byte_t* buf,
                     size_t buf_len,
                     size_t offset)
    {
        assert(b.size() <= std::numeric_limits<ST>::max());
        if (offset + b.size() > buf_len) gu_throw_fatal;
        offset = serialize(static_cast<ST>(b.size()), buf, buf_len, offset);
        copy(b.begin(), b.end(), buf + offset);
        offset += b.size();
        return offset;
    }

    template<typename ST>
    size_t unserialize(const gu::byte_t* buf,
                       size_t buf_len,
                       size_t offset,
                       gu::Buffer& b)
    {
        ST len(0);
        if (offset + serial_size(len) > buf_len) gu_throw_fatal;

        offset = unserialize(buf, buf_len, offset, len);
        if (offset + len > buf_len) gu_throw_fatal;
        if (len > std::numeric_limits<ST>::max()) gu_throw_fatal;
        b.resize(len);
        copy(buf + offset, buf + offset + len, b.begin());
        offset += len;
        return offset;
    }

    template<typename ST>
    size_t serial_size(const gu::Buffer& sb)
    {
        assert(sb.size() <= std::numeric_limits<ST>::max());
        return serial_size(ST()) + sb.size();
    }
}
#endif // GALERA_SERIALIZATION_HPP
