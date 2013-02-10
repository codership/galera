//
// Copyright (C) 2011-2013 Codership Oy <info@codership.com>
//

//!
// @file Variable-length quantity encoding for integers
//
// Unsigned integers: Implementation uses using unsigned LEB128,
// see for example http://en.wikipedia.org/wiki/LEB128.
//
// Signed integers: TODO
//

#ifndef GU_VLQ_HPP
#define GU_VLQ_HPP

#include "gu_buffer.hpp"
#include "gu_throw.hpp"

#include "gu_macros.h"

#define GU_VLQ_CHECKS
#define GU_VLQ_ALEX

namespace gu
{
    //!
    // @brief Retun number of bytes required to represent given value in ULEB128
    //        representation.
    //
    // @param value Unsigned value
    //
    // @return Number of bytes required for value representation
    //
    template <typename UI>
    inline size_t uleb128_size(UI value)
    {
        size_t i(1);
        value >>= 7;

        for (; value != 0; value >>= 7, ++i) {}

        return i;
    }

    //!
    // @brief Encode unsigned type to ULEB128 representation
    //
    // @param value
    // @param buf
    // @param buflen
    // @param offset
    //
    // @return Offset
    //
    template <typename UI>
    inline size_t uleb128_encode(UI       value,
                                 byte_t*  buf,
                                 size_t   buflen,
                                 size_t   offset)
    {
#ifdef GU_VLQ_ALEX
        assert (offset < buflen);
        buf[offset] = value & 0x7f;

        while (value >>= 7)
        {
            buf[offset] |= 0x80;
            ++offset;
#ifdef GU_VLQ_CHECKS
            if (gu_unlikely(offset >= buflen)) gu_throw_fatal;
#else
            assert(offset < buflen);
#endif /* GU_VLQ_CHECKS */
            buf[offset] = value & 0x7f;
        }

        return offset + 1;
#else /* GU_VLQ_ALEX */
        do
        {
#ifdef GU_VLQ_CHECKS
            if (gu_unlikely(offset >= buflen)) gu_throw_fatal;
#else
            assert(offset < buflen);
#endif /* GU_VLQ_CHECKS */
            buf[offset] = value & 0x7f;
            value >>= 7;
            if (gu_unlikely(value != 0))
            {
                buf[offset] |= 0x80;
            }
            ++offset;
        }
        while (value != 0);

        return offset;
#endif /* GU_VLQ_ALEX */
    }

    template <typename UI>
    inline size_t uleb128_encode(UI       value,
                                 byte_t*  buf,
                                 size_t   buflen)
    {
        return uleb128_encode(value, buf, buflen, 0);
    }


    /* checks helper for the uleb128_decode() below */
    extern void uleb128_decode_checks (const byte_t* buf,
                                       size_t        buflen,
                                       size_t        offset,
                                       size_t        avail_bits);

    //!
    // @brief Decode unsigned type from ULEB128 representation
    //
    // @param buf
    // @param buflen
    // @param offset
    // @param value
    //
    // @return Offset
    //
    template <typename UI>
    inline size_t uleb128_decode(const byte_t* buf,
                                 size_t        buflen,
                                 size_t        offset,
                                 UI&           value)
    {
        // initial check for overflow, at least one byte must be readable
#ifdef GU_VLQ_CHECKS
        if (gu_unlikely(offset >= buflen)) gu_throw_fatal;
#endif

#ifdef GU_VLQ_ALEX
        value = buf[offset] & 0x7f;
        size_t shift(0);

        while (buf[offset] & 0x80)
        {
            ++offset;
            shift +=7;

#ifdef GU_VLQ_CHECKS
            ssize_t left_bits((sizeof(UI) << 3) - shift);
            if (gu_unlikely(offset >= buflen || left_bits < 7))
                uleb128_decode_checks (buf, buflen, offset, left_bits);
#endif
            value |= (UI(buf[offset] & 0x7f) << shift);
        }

        return offset + 1;
#else /* GU_VLQ_ALEX */
        value = 0;
        size_t shift(0);

        while (true)
        {
            value |= (UI(buf[offset] & 0x7f) << shift);
            if (gu_likely((buf[offset] & 0x80) == 0))
            {
                // last byte
                ++offset;
                break;
            }
            ++offset;
            shift += 7;

#ifdef GU_VLQ_CHECKS
            ssize_t left_bits((sizeof(UI) << 3) - shift);
            if (gu_unlikely(offset >= buflen || left_bits < 7))
                uleb128_decode_checks (buf, buflen, offset, left_bits);
#endif
        }

        return offset;
#endif /* GU_VLQ_ALEX */
    }

    template <typename UI>
    inline size_t uleb128_decode(const byte_t* buf,
                                 size_t        buflen,
                                 UI&           value)
    {
        return uleb128_decode(buf, buflen, 0, value);
    }
}

#endif // GU_VLQ_HPP
