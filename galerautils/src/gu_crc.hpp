/*
 * Copyright (C) 2013 Codership Oy <info@codership.com>
 *
 * @file header for various CRC stuff
 *
 * $Id$
 */

#ifndef GU_CRC_HPP
#define GU_CRC_HPP

#include "gu_crc32c.h"

namespace gu
{

class CRC32C
{
public:

    CRC32C() : state_(GU_CRC32C_INIT) {}

    void append(const void* const data, size_t const size)
    {
        gu_crc32c_append (&state_, data, size);
    }

    uint32_t get() const { return gu_crc32c_get(state_); }

    uint32_t operator() () const { return get(); }

    static uint32_t digest(const void* const data, size_t const size)
    {
        return gu_crc32c(data, size);
    }

private:

    gu_crc32c_t state_;

}; /* class CRC32C */

} /* namespace gu */

#endif /* GU_CRC_HPP */
