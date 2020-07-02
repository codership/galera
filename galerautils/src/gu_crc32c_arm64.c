/*
 * Copyright (C) 2020 Codership Oy <info@codership.com>
 */

/**
 * @file Hardware-accelerated implementation of CRC32C algorithm using arm64
 *       instructions.
 *
 * Defines gu_crc32c_hardware() that returns pointer to gu_crc32c_func_t if
 * available on a given CPU.
 */

#include "gu_crc32c.h"

#if defined(GU_CRC32C_ARM64)

#include "gu_log.h"

#include <assert.h>
#include <stdbool.h>

#include <arm_acle.h>
#include <arm_neon.h>

static inline gu_crc32c_t
crc32c_arm64_tail7(gu_crc32c_t state, const uint8_t* ptr, size_t len)
{
    assert(len < 7);

    if (len >= 4)
    {
        state = __crc32cw(state, *(uint32_t *)ptr);
        ptr += 4;
        len -= 4;
    }

    switch (len)
    {
    case 3:
        state = __crc32cb(state, *ptr);
        ptr++;
        /* fall through */
    case 2:
        state = __crc32ch(state, *(uint16_t*)ptr);
        break;
    case 1:
        state = __crc32cb(state, *ptr);;
    }

    return state;
}

gu_crc32c_t
gu_crc32c_arm64(gu_crc32c_t state, const void* data, size_t len)
{
    static size_t const arg_size = sizeof(uint64_t);
    const uint8_t* ptr = (const uint8_t*)data;

    /* apparently no ptr misalignment protection is needed */
    while (len >= arg_size)
    {
        state = __crc32cd(state, *(uint64_t*)ptr);
        len -= arg_size;
        ptr += arg_size;
    }

    assert(len < 8);

    return crc32c_arm64_tail7(state, ptr, len);
}

#include <sys/auxv.h>

gu_crc32c_func_t
gu_crc32c_hardware()
{
    unsigned long int const hwcaps = getauxval(AT_HWCAP);
    if (hwcaps & HWCAP_CRC32)
    {
        gu_info ("CRC-32C: using hardware acceleration.");
        return gu_crc32c_arm64;
    }
    else
    {
        return NULL;
    }
}

#endif /* GU_CRC32C_ARM64 */
