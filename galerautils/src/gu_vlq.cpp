//
// Copyright (C) 2013 Codership Oy <info@codership.com>
//

//!
// @file Variable-length quantity encoding for integers
//
// Unsigned integers: Implementation uses using unsigned LEB128,
// see for example http://en.wikipedia.org/wiki/LEB128.
//
// Signed integers: TODO
//

#include "gu_vlq.hpp"

namespace gu
{
    /* checks helper for the uleb128_decode() */
    void uleb128_decode_checks (const byte_t* buf,
                                size_t        buflen,
                                size_t        offset,
                                size_t        avail_bits)
    {
        // Check if trying to read past last byte in buffer without
        // encountering byte without 0x80 bit set.
        if (offset >= buflen)
        {
            gu_throw_error(EINVAL)
                << "read value is not uleb128 representation, missing "
                << "terminating byte before end of input";
        }

        assert(avail_bits > 0);

        if (avail_bits < 7)
        {
            // mask to check if the remaining value can be represented
            // with available bits
            gu::byte_t mask(~((1 << avail_bits) - 1));
            if ((buf[offset] & mask) != 0)
            {
                gu_throw_error(EOVERFLOW)
                    << "read value not representable with avail bits: "
                    << avail_bits
                    << " mask: 0x"
                    << std::hex << static_cast<int>(mask)
                    << " buf: 0x"
                    << std::hex << static_cast<int>(buf[offset])
                    << " excess: 0x"
                    << std::hex << static_cast<int>(mask & buf[offset]);
            }
        }
    }
} /* namespace gu */
