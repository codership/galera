//
// Copyright (C) 2011 Codership Oy <info@codership.com>
//

//!
// @file Variable-length quantity encoding for integers
//
// Unsigned integers: Implementation uses using unsigned LEB128, see for example
// http://en.wikipedia.org/wiki/LEB128.
//
// Signed integers: TODO
//

#ifndef GU_VLQ_HPP
#define GU_VLQ_HPP

#include "gu_buffer.hpp"
#include "gu_throw.hpp"
#include "gu_logger.hpp"

#include "gu_macros.h"

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
        size_t i(0);
        do
        {
            value >>= 7;
            ++i;
        }
        while (value != 0);
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
        do
        {
            if (offset >= buflen) gu_throw_fatal;
            buf[offset] = value & 0x7f;
            value >>= 7;
            if (value != 0)
            {
                buf[offset] |= 0x80;
            }
            ++offset;
        }
        while (value != 0);
        return offset;
    }

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
        value = 0;
        size_t shift(0);

        // initial check for overflow, at least one byte must be readable
        if (offset >= buflen) gu_throw_fatal;

        while (true)
        {
            value |= (static_cast<UI>(buf[offset] & 0x7f) << shift);
            if ((buf[offset] & 0x80) == 0)
            {
                // last byte
                ++offset;
                break;
            }
            ++offset;

            // Check if trying to read past last byte in buffer without
            // encountering byte without 0x80 bit set.
            if (gu_unlikely(offset >= buflen))
            {
                gu_throw_error(EINVAL)
                    << "read value is not uleb128 representation, missing "
                    << "terminating byte before end of input";
            }

            //
            // determine proper bit shift
            //

            // type width
            static const size_t width(sizeof(value) * 8);
            // bits available after shift
            const ssize_t avail_bits(width - (shift + 7));
            assert(avail_bits > 0);

            if (gu_unlikely(avail_bits < 7))
            {
                // mask to check if the remaining value can be represented
                // with available bits
                gu::byte_t mask(~((1 << avail_bits) - 1));
                if ((buf[offset] & mask) != 0)
                {
                    gu_throw_error(ERANGE)
                        << "read value not representable with "
                        << width
                        << " bits, shift: " << shift + 7 << " avail bits: "
                        << avail_bits
                        << " mask: 0x"
                        << std::hex << static_cast<int>(mask)
                        << " buf: 0x"
                        << std::hex << static_cast<int>(buf[offset])
                        << " excess: 0x"
                        << std::hex << static_cast<int>(mask & buf[offset]);
                }
            }
            shift += 7;
        }
        return offset;
    }
}

#endif // GU_VLQ_HPP
