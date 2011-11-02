//
// Copyright (C) 2010 Codership Oy <info@codership.com>
//

#ifndef GALERA_SERIALIZATION_HPP
#define GALERA_SERIALIZATION_HPP

#include "gu_throw.hpp"
#include "gu_buffer.hpp"

#include <limits>

namespace galera
{
    // Helper templates to serialize integer types
    template<typename I> size_t serialize(const I& i,
                                          gu::byte_t* buf,
                                          size_t buf_len,
                                          size_t offset)
    {
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        *reinterpret_cast<I*>(buf + offset) = i;
        return (offset + sizeof(i));
    }

    template<typename I> size_t unserialize(const gu::byte_t* buf,
                                            size_t buf_len,
                                            size_t offset,
                                            I& i)
    {
        if (offset + sizeof(i) > buf_len) gu_throw_fatal;
        i = *reinterpret_cast<const I*>(buf + offset);
        return (offset + sizeof(i));
    }

    template<typename I> size_t serial_size(const I& i)
    {
        return sizeof(i);
    }

#if 0
    template<typename ST>
    size_t serialize(const void* data, ST data_len, gu::byte_t* buf, 
                     size_t buf_len,
                     size_t offset)
    {
        offset = serialize(data_len, buf, buf_len, offset);
        if (offset + data_len > buf_len) gu_throw_fatal;
        memcpy(buf + offset, data, data_len);
        return (offset + data_len);
    }

    template <typename ST>
    size_t unserialize(const gu::byte_t* buf, size_t buf_len, size_t offset,
                       const void*& data, ST& data_len)
    {
        offset = unserialize(buf, buf_len, offset, data_len);
        if (offset + data_len > buf_len) gu_throw_fatal;
        data = buf + offset;
        return (offset + data_len);
    }

    template <typename ST>
    size_t serial_size(const void* data, ST data_len)
    {
        return (serial_size(data_len) + data_len);
    }
#endif

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


    template<typename I, typename ST>
    size_t serialize(I begin, I end,
                     gu::byte_t* buf, size_t buf_len, size_t offset)
    {
        if (static_cast<size_t>(std::distance(begin, end)) > 
            std::numeric_limits<ST>::max()) gu_throw_fatal;
        offset = serialize(static_cast<ST>(std::distance(begin, end)), 
                           buf, buf_len, offset);
        for (I i = begin; i != end; ++i)
        {
            offset = serialize(*i, buf, buf_len, offset);
        }
        return offset;
    }

    template<class C, typename ST, typename BI>
    size_t unserialize(const gu::byte_t* buf, size_t buf_len, size_t offset,
                       BI bi)
    {
        ST len;
        offset = unserialize(buf, buf_len, offset, len);
        // s.reserve(len);
        for (ST i = 0; i < len; ++i)
        {
            C c;
            offset = unserialize(buf, buf_len, offset, c);
            *bi++ = c;
        }
        return offset;
    }

    template<typename I, typename ST>
    size_t serial_size(I begin, I end)
    {
        size_t ret(serial_size(ST()));
        for (I i = begin; i != end; ++i)
        {
            ret += serial_size(*i);
        }
        return ret;
    }
}
#endif // GALERA_SERIALIZATION_HPP
